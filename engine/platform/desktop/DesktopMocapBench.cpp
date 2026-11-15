// =============================================================================
//  Aurea / platform / desktop / DesktopMocapBench.cpp
//
//  O Motion Capture do Aurea PC de ponta a ponta, sem JVM e sem janela: a
//  MESMA sessão da aurea.dll (DesktopMocap.cpp) — Media Foundation → BlazePose
//  → quadros do motor — num vídeo, e (com --fbx) o MESMO solver do motor
//  (rig::solve) num personagem de verdade. Não vai no app.
//
//  Mede a qualidade (13/11, pedido do dono "melhore o rastreio"):
//   - pontos de imagem: tremor parado (aceleração média/p95 em troncos por
//     quadro²) e em movimento, "estalos" (aceleração > 0,25 tronco), quadros
//     sem pessoa, vezes do detector, trocas esquerda↔direita (cotovelo,
//     punho, joelho, tornozelo trocam de lugar num quadro);
//   - pontos de mundo (se houver): tremor em mm e variação do comprimento
//     dos ossos (CV);
//   - solver: erro de reprojeção das juntas do personagem resolvido sobre os
//     pontos do vídeo (escala + translação por quadro, em troncos), velocidade
//     e aceleração angular dos ossos (graus/quadro), "viradas" (> 45° num
//     quadro) e o ângulo até as direções 3D do BlazePose (referência, não verdade).
//  E desenha a folha de contato (PNG): quadros a cada N, pontos do vídeo
//  (laranja = lado esquerdo da pessoa, azul = direito) e o personagem
//  resolvido por cima (magenta) com a vista de LADO no canto (verde).
//
//  Uso: aurea_pc_mocap_bench <modelos> <video> [opções]
//    --legacy            o pipeline de 12/11 (384 px, full, sem mundo)
//    --reduced           modo reduzido
//    --full | --heavy    modelo de landmarks
//    --side N            lado maior da imagem da análise (0 = inteira)
//    --no-aa --no-refine --no-world --no-video-tracker --no-roi-filter
//    --no-swaps --no-border --recheck N
//    --solver-2d         o solver ignora os pontos de mundo
//    --smooth S          suavização do solver (0..1, padrão 0,5 = o app)
//    --fbx caminho       personagem para o solver
//    --sheet saida.png   folha de contato   --tiles N (padrão 24)
//    --csv arquivo       acrescenta uma linha com as métricas   --label nome
//  Saída 0 = todos os quadros finitos e pessoa em pelo menos metade deles.
// =============================================================================
#include "DesktopMocap.hpp"
#include "WindowsMedia.hpp"
#include "aurea/export/ImageEncode.hpp"
#include "aurea/rig/AutoRig.hpp"
#include "aurea/rig/Humanoid.hpp"
#include "aurea/rig/Ik.hpp"
#include "aurea/rig/Mocap.hpp"
#include "aurea/scene3d/Importer.hpp"
#include "aurea/scene3d/SceneAsset.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace aurea;
namespace lm = aurea::rig::lm;
using HB = rig::HumanoidBone;

namespace {

constexpr f32 kToDeg = 57.29577951f;

f32 pct(std::vector<f32> v, f32 p) {
    if (v.empty()) return 0.0f;
    const usize k = static_cast<usize>(std::clamp(p, 0.0f, 1.0f) * static_cast<f32>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}
f32 mean(const std::vector<f32>& v) {
    if (v.empty()) return 0.0f;
    f64 s = 0.0;
    for (f32 x : v) s += x;
    return static_cast<f32>(s / static_cast<f64>(v.size()));
}

struct V2 { f32 x = 0, y = 0; };
V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
V2 operator*(V2 a, f32 s) { return {a.x * s, a.y * s}; }
f32 len(V2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }

// ---- Desenho ---------------------------------------------------------------------

struct Canvas {
    u32 w = 0, h = 0;
    std::vector<u8> px;
    void init(u32 W, u32 H) { w = W; h = H; px.assign(static_cast<usize>(W) * H * 4, 0); for (usize i = 3; i < px.size(); i += 4) px[i] = 255; }
    void dot(i32 x, i32 y, const u8 c[3], f32 a) {
        if (x < 0 || y < 0 || x >= static_cast<i32>(w) || y >= static_cast<i32>(h)) return;
        u8* p = px.data() + (static_cast<usize>(y) * w + static_cast<usize>(x)) * 4;
        for (int k = 0; k < 3; ++k) p[k] = static_cast<u8>(p[k] + (c[k] - p[k]) * a);
    }
    void disc(f32 cx, f32 cy, f32 r, const u8 c[3], f32 a = 1.0f) {
        for (i32 y = static_cast<i32>(cy - r); y <= static_cast<i32>(cy + r); ++y)
            for (i32 x = static_cast<i32>(cx - r); x <= static_cast<i32>(cx + r); ++x)
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) dot(x, y, c, a);
    }
    void line(f32 x0, f32 y0, f32 x1, f32 y1, f32 r, const u8 c[3], f32 a = 1.0f) {
        const f32 L = std::max(1.0f, std::hypot(x1 - x0, y1 - y0));
        const i32 n = static_cast<i32>(L);
        for (i32 i = 0; i <= n; ++i) {
            const f32 t = static_cast<f32>(i) / static_cast<f32>(std::max(1, n));
            const f32 x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t;
            for (i32 dy = static_cast<i32>(-r); dy <= static_cast<i32>(r); ++dy)
                for (i32 dx = static_cast<i32>(-r); dx <= static_cast<i32>(r); ++dx)
                    if (dx * dx + dy * dy <= r * r + 0.5f) dot(static_cast<i32>(x) + dx, static_cast<i32>(y) + dy, c, a);
        }
    }
    void blit(const std::vector<u8>& src, u32 sw, u32 sh, u32 ox, u32 oy) {
        for (u32 y = 0; y < sh && oy + y < h; ++y)
            std::memcpy(px.data() + (static_cast<usize>(oy + y) * w + ox) * 4, src.data() + static_cast<usize>(y) * sw * 4,
                        static_cast<usize>(std::min(sw, w - ox)) * 4);
    }
};

/// Redução por média de área (a folha de contato).
std::vector<u8> shrink(const desktop_mocap::FrameView& v, u32 tw, u32 th) {
    std::vector<u8> out(static_cast<usize>(tw) * th * 4, 255);
    const f32 sx = static_cast<f32>(v.width) / static_cast<f32>(tw), sy = static_cast<f32>(v.height) / static_cast<f32>(th);
    for (u32 y = 0; y < th; ++y)
        for (u32 x = 0; x < tw; ++x) {
            const u32 x0 = static_cast<u32>(x * sx), x1 = std::max(x0 + 1, std::min(v.width, static_cast<u32>((x + 1) * sx)));
            const u32 y0 = static_cast<u32>(y * sy), y1 = std::max(y0 + 1, std::min(v.height, static_cast<u32>((y + 1) * sy)));
            u32 acc[3]{0, 0, 0}, n = 0;
            for (u32 yy = y0; yy < y1; ++yy)
                for (u32 xx = x0; xx < x1; ++xx) {
                    const u8* p = v.rgba + (static_cast<usize>(yy) * v.width + xx) * 4;
                    acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; ++n;
                }
            u8* o = out.data() + (static_cast<usize>(y) * tw + x) * 4;
            // Escurece um pouco: os traços aparecem melhor.
            for (int k = 0; k < 3; ++k) o[k] = static_cast<u8>(0.7f * static_cast<f32>(acc[k]) / static_cast<f32>(std::max(1u, n)));
        }
    return out;
}

struct Tile {
    usize frame = 0;
    std::vector<u8> rgba;
};

// ---- Solver: posições pela cadeia de deltas -----------------------------------------

std::array<Vec3, rig::kHumanoidBoneCount> fk(const rig::HumanoidRest& R, const std::array<Quat, rig::kHumanoidBoneCount>& d) {
    std::array<Vec3, rig::kHumanoidBoneCount> out{};
    for (usize b = 0; b < rig::kHumanoidBoneCount; ++b) {
        HB a = rig::parent_of(static_cast<HB>(b));
        while (a != HB::Count && !R.present[static_cast<usize>(a)]) a = rig::parent_of(a);
        if (a == HB::Count) { out[b] = R.position[b]; continue; }
        const usize ai = static_cast<usize>(a);
        out[b] = out[ai] + rig::rotate(d[ai], R.position[b] - R.position[ai]);
    }
    return out;
}

struct Args {
    std::string models, video, fbx, sheet, csv, dump, label = "run";
    desktop_mocap::Options opt = desktop_mocap::Options::defaults(false);
    bool solver2d = false;
    f32 smooth = 0.5f;
    u32 tiles = 24;
    usize from = 0, to = 0;   ///< folha só com estes quadros (0, 0 = o vídeo todo)
};

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "uso: aurea_pc_mocap_bench <modelos> <video> [opcoes] (ver o cabecalho do arquivo)\n");
        return 2;
    }
    windows::initialize_media();
    Args a;
    a.models = windows::utf8(argv[1]);
    a.video = windows::utf8(argv[2]);
    bool reduced = false;
    for (int i = 3; i < argc; ++i) {
        const std::string s = windows::utf8(argv[i]);
        auto val = [&]() { return i + 1 < argc ? windows::utf8(argv[++i]) : std::string(); };
        if (s == "--legacy") a.opt = desktop_mocap::Options::legacy(reduced);
        else if (s == "--reduced" || s == "1") { reduced = true; a.opt = desktop_mocap::Options::defaults(true); }
        else if (s == "0") {}
        else if (s == "--full") { a.opt.heavy = false; }
        else if (s == "--heavy") { a.opt.heavy = true; }
        else if (s == "--side") a.opt.maxSide = static_cast<u32>(std::stoul(val()));
        else if (s == "--no-aa") a.opt.antialias = false;
        else if (s == "--no-refine") a.opt.refine = false;
        else if (s == "--no-world") a.opt.world = false;
        else if (s == "--no-video-tracker") a.opt.videoTracker = false;
        else if (s == "--no-roi-filter") a.opt.filterRoi = false;
        else if (s == "--no-swaps") a.opt.fixSwaps = false;
        else if (s == "--no-border") a.opt.borderRedetect = false;
        else if (s == "--recheck") a.opt.recheckEvery = static_cast<u32>(std::stoul(val()));
        else if (s == "--solver-2d") a.solver2d = true;
        else if (s == "--smooth") a.smooth = std::stof(val());
        else if (s == "--fbx") a.fbx = val();
        else if (s == "--sheet") a.sheet = val();
        else if (s == "--tiles") a.tiles = static_cast<u32>(std::stoul(val()));
        else if (s == "--csv") a.csv = val();
        else if (s == "--label") a.label = val();
        else if (s == "--dump") a.dump = val();
        else if (s == "--range") { a.from = std::stoul(val()); a.to = std::stoul(val()); }
        else { std::fprintf(stderr, "opcao desconhecida: %s\n", s.c_str()); return 2; }
    }

    // ---- Análise ----------------------------------------------------------------------
    const auto t0 = std::chrono::steady_clock::now();
    f32 info[desktop_mocap::kInfoFloats]{};
    desktop_mocap::Session* s = desktop_mocap::open(a.models, a.video, a.opt, info);
    const auto t1 = std::chrono::steady_clock::now();
    if (!s) {
        std::printf("abrir falhou: erro %.0f\n", info[0]);
        return 3;
    }
    std::printf("video %.0fx%.0f %.2f fps %.2f s; pose em %s; abrir %.0f ms\n", info[3], info[4], info[5], info[1],
                info[2] > 0.5f ? "Vulkan" : "CPU", std::chrono::duration<double, std::milli>(t1 - t0).count());
    const usize expected = static_cast<usize>(std::max(1.0, static_cast<f64>(info[1]) * a.opt.fps));
    const usize span = a.to > a.from ? a.to - a.from : expected;
    const usize every = std::max<usize>(1, span / std::max(1u, a.tiles));
    constexpr u32 kTileH = 300;
    std::vector<f32> buffer(4 * rig::kMocapFrameFloats);
    std::vector<f32> all;
    std::vector<Tile> tiles;
    f32 state[desktop_mocap::kStateFloats]{};
    usize analysed = 0;
    u32 tileW = 0;
    for (;;) {
        const i32 n = desktop_mocap::next(s, buffer.data(), a.opt.world ? 2 : 1, state);
        if (n < 0) { std::printf("erro do decoder\n"); desktop_mocap::close(s); return 4; }
        if (n > 0) {
            all.insert(all.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n) * rig::kMocapFrameFloats);
            if (!a.sheet.empty() && analysed >= a.from && (a.to <= a.from || analysed < a.to) && (analysed - a.from) % every == 0 && tiles.size() < a.tiles) {
                const desktop_mocap::FrameView v = desktop_mocap::last_image(s);
                if (v.rgba) {
                    tileW = std::max<u32>(1, static_cast<u32>(std::lround(static_cast<f64>(kTileH) * v.width / v.height)));
                    tiles.push_back(Tile{analysed, shrink(v, tileW, kTileH)});
                }
            }
            ++analysed;
        }
        if (state[2] > 0.5f) break;
    }
    const auto t2 = std::chrono::steady_clock::now();
    const u32 swaps = desktop_mocap::swaps_fixed(s);
    const f64 imgMs = desktop_mocap::image_ms(s);
    desktop_mocap::close(s);
    bool finite = true;
    for (f32 v : all) finite &= std::isfinite(v) != 0;
    if (!a.dump.empty()) {
        // Os quadros crus das pontes (floats), para comparar execuções.
        std::ofstream d(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(a.dump.data()), a.dump.size())), std::ios::binary);
        d.write(reinterpret_cast<const char*>(all.data()), static_cast<std::streamsize>(all.size() * sizeof(f32)));
    }
    std::vector<rig::PoseFrame> frames;
    rig::unpack_frames(all.data(), static_cast<u32>(all.size() / rig::kMocapFrameFloats), frames);
    const usize F = frames.size();
    const f64 wall = std::chrono::duration<double, std::milli>(t2 - t1).count();
    const f32 aspect = state[1] > 0.0f ? state[1] : 1.0f;
    usize worldFrames = 0;
    for (const auto& f : frames) worldFrames += f.has_aux_world ? 1u : 0u;
    std::printf("%zu quadros (%zu com mundo); com pessoa %.0f; detector %.0f vezes; trocas desfeitas %u; rede %.1f ms/quadro; "
                "imagem %.1f ms/quadro; total %.1f ms/quadro; aspecto %.3f\n",
                F, worldFrames, state[5], state[4], swaps, state[3], F ? imgMs / static_cast<f64>(F) : 0.0,
                F ? wall / static_cast<f64>(F) : 0.0, aspect);

    // ---- Métricas dos pontos de imagem ----------------------------------------------------
    auto person = [&](usize f) { return frames[f].pts[lm::LeftShoulder].confidence > 0.0f || frames[f].pts[lm::LeftHip].confidence > 0.0f; };
    auto P = [&](usize f, u32 j) { return V2{frames[f].pts[j].x * aspect, frames[f].pts[j].y}; };
    std::vector<f32> torsos;
    for (usize f = 0; f < F; ++f) {
        if (!person(f)) continue;
        const V2 ms = (P(f, lm::LeftShoulder) + P(f, lm::RightShoulder)) * 0.5f, mh = (P(f, lm::LeftHip) + P(f, lm::RightHip)) * 0.5f;
        torsos.push_back(len(ms - mh));
    }
    const f32 T = std::max(pct(torsos, 0.5f), 1e-3f);
    const u32 J[] = {lm::Nose, lm::LeftShoulder, lm::RightShoulder, lm::LeftElbow, lm::RightElbow, lm::LeftWrist, lm::RightWrist,
                     lm::LeftHip, lm::RightHip, lm::LeftKnee, lm::RightKnee, lm::LeftAnkle, lm::RightAnkle};
    // Parado x em movimento: velocidade do tronco numa média de ±5 quadros (sem o tremor).
    std::vector<f32> bodySpeed(F, 1e9f);
    {
        std::vector<V2> c(F);
        for (usize f = 0; f < F; ++f)
            if (person(f)) c[f] = (P(f, lm::LeftShoulder) + P(f, lm::RightShoulder) + P(f, lm::LeftHip) + P(f, lm::RightHip)) * 0.25f;
        std::vector<V2> sm(F);
        std::vector<u8> ok(F, 0);
        for (usize f = 0; f < F; ++f) {
            V2 acc;
            u32 n = 0;
            bool full = true;
            for (i64 d = -5; d <= 5; ++d) {
                const i64 g = static_cast<i64>(f) + d;
                if (g < 0 || g >= static_cast<i64>(F) || !person(static_cast<usize>(g))) { full = false; continue; }
                acc = acc + c[static_cast<usize>(g)];
                ++n;
            }
            if (full && n) { sm[f] = acc * (1.0f / static_cast<f32>(n)); ok[f] = 1; }
        }
        for (usize f = 1; f < F; ++f)
            if (ok[f] && ok[f - 1]) bodySpeed[f] = len(sm[f] - sm[f - 1]) / T;
    }
    std::vector<f32> accStill, accMove, velStill, accWrist, accAnkle;
    usize pops = 0, still = 0, lost = 0, swapsSeen = 0;
    for (usize f = 0; f < F; ++f) lost += person(f) ? 0u : 1u;
    for (usize f = 1; f + 1 < F; ++f) {
        if (!person(f - 1) || !person(f) || !person(f + 1)) continue;
        f32 acc = 0.0f, vel = 0.0f;
        for (const u32 j : J) {
            acc += len(P(f + 1, j) - P(f, j) * 2.0f + P(f - 1, j)) / T;
            vel += len(P(f, j) - P(f - 1, j)) / T;
        }
        acc /= static_cast<f32>(std::size(J));
        vel /= static_cast<f32>(std::size(J));
        accWrist.push_back(0.5f * (len(P(f + 1, lm::LeftWrist) - P(f, lm::LeftWrist) * 2.0f + P(f - 1, lm::LeftWrist)) +
                                   len(P(f + 1, lm::RightWrist) - P(f, lm::RightWrist) * 2.0f + P(f - 1, lm::RightWrist))) / T);
        accAnkle.push_back(0.5f * (len(P(f + 1, lm::LeftAnkle) - P(f, lm::LeftAnkle) * 2.0f + P(f - 1, lm::LeftAnkle)) +
                                   len(P(f + 1, lm::RightAnkle) - P(f, lm::RightAnkle) * 2.0f + P(f - 1, lm::RightAnkle))) / T);
        if (acc > 0.25f) ++pops;
        if (bodySpeed[f] < 0.01f) { accStill.push_back(acc); velStill.push_back(vel); ++still; }
        else accMove.push_back(acc);
    }
    for (usize f = 1; f < F; ++f) {
        if (!person(f - 1) || !person(f)) continue;
        for (const auto& pr : {std::array<u32, 2>{lm::LeftElbow, lm::RightElbow}, std::array<u32, 2>{lm::LeftWrist, lm::RightWrist},
                               std::array<u32, 2>{lm::LeftKnee, lm::RightKnee}, std::array<u32, 2>{lm::LeftAnkle, lm::RightAnkle}}) {
            const f32 same = len(P(f, pr[0]) - P(f - 1, pr[0])) + len(P(f, pr[1]) - P(f - 1, pr[1]));
            const f32 swp = len(P(f, pr[0]) - P(f - 1, pr[1])) + len(P(f, pr[1]) - P(f - 1, pr[0]));
            if (same > 0.2f * T && swp < 0.5f * same) ++swapsSeen;
        }
    }
    std::printf("PONTOS: tronco %.3f da altura; parado %zu quadros: acel media %.4f p95 %.4f, vel media %.4f (troncos/quadro); "
                "em movimento: acel media %.4f p95 %.4f; punhos acel p95 %.4f, tornozelos p95 %.4f; estalos %zu; sem pessoa %zu; "
                "trocas E/D vistas %zu\n",
                T, still, mean(accStill), pct(accStill, 0.95f), mean(velStill), mean(accMove), pct(accMove, 0.95f),
                pct(accWrist, 0.95f), pct(accAnkle, 0.95f), pops, lost, swapsSeen);
    // Mundo: tremor (mm) e comprimento dos ossos.
    f32 worldAccStill = 0.0f, worldBoneCv = 0.0f, visWrist = 0.0f, visAnkle = 0.0f;
    if (worldFrames) {
        std::vector<f32> wa;
        for (usize f = 1; f + 1 < F; ++f) {
            if (!frames[f - 1].has_aux_world || !frames[f].has_aux_world || !frames[f + 1].has_aux_world) continue;
            if (!person(f - 1) || !person(f) || !person(f + 1) || bodySpeed[f] >= 0.01f) continue;
            f32 acc = 0.0f;
            for (const u32 j : J) {
                const auto& p0 = frames[f - 1].world[j];
                const auto& p1 = frames[f].world[j];
                const auto& p2 = frames[f + 1].world[j];
                acc += std::sqrt((p2.x - 2 * p1.x + p0.x) * (p2.x - 2 * p1.x + p0.x) + (p2.y - 2 * p1.y + p0.y) * (p2.y - 2 * p1.y + p0.y) +
                                 (p2.z - 2 * p1.z + p0.z) * (p2.z - 2 * p1.z + p0.z));
            }
            wa.push_back(1000.0f * acc / static_cast<f32>(std::size(J)));
        }
        worldAccStill = mean(wa);
        const u32 seg[][2] = {{lm::LeftShoulder, lm::LeftElbow}, {lm::LeftElbow, lm::LeftWrist}, {lm::RightShoulder, lm::RightElbow},
                              {lm::RightElbow, lm::RightWrist}, {lm::LeftHip, lm::LeftKnee}, {lm::LeftKnee, lm::LeftAnkle},
                              {lm::RightHip, lm::RightKnee}, {lm::RightKnee, lm::RightAnkle}};
        f32 cvSum = 0.0f;
        for (const auto& sg : seg) {
            std::vector<f32> L;
            for (usize f = 0; f < F; ++f) {
                if (!frames[f].has_aux_world || !person(f)) continue;
                const auto& p = frames[f].world[sg[0]];
                const auto& q = frames[f].world[sg[1]];
                L.push_back(std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z)));
            }
            const f32 m = mean(L);
            f32 var = 0.0f;
            for (f32 x : L) var += (x - m) * (x - m);
            cvSum += m > 0 ? std::sqrt(var / std::max<usize>(1, L.size())) / m : 0.0f;
        }
        worldBoneCv = cvSum / static_cast<f32>(std::size(seg));
        std::vector<f32> vw, va;
        for (usize f = 0; f < F; ++f) {
            if (!frames[f].has_aux_world || !person(f)) continue;
            vw.push_back(0.5f * (frames[f].world[lm::LeftWrist].confidence + frames[f].world[lm::RightWrist].confidence));
            va.push_back(0.5f * (frames[f].world[lm::LeftAnkle].confidence + frames[f].world[lm::RightAnkle].confidence));
        }
        visWrist = mean(vw);
        visAnkle = mean(va);
        std::printf("MUNDO: parado acel media %.2f mm/quadro2; CV do comprimento dos ossos %.3f; visibilidade punhos %.2f tornozelos %.2f\n",
                    worldAccStill, worldBoneCv, visWrist, visAnkle);
    }

    // ---- Solver no personagem ----------------------------------------------------------------
    f32 rootAccStill = 0, reproMean = 0, reproP95 = 0, angVel = 0, angVelP95 = 0, angAcc = 0, angAccP95 = 0, angAccStill = 0, agree = 0;
    usize flips = 0;
    bool solved = false;
    rig::MocapResult result;
    rig::HumanoidRest rest;
    rig::ModelBasis basis;
    std::vector<std::array<Vec3, rig::kHumanoidBoneCount>> pos;
    std::vector<std::array<f32, 3>> fit(F, {0, 0, 0});   // escala, tx, ty
    if (!a.fbx.empty()) {
        scene3d::ImportOptions io;
        io.optimize = false;
        io.generateLods = false;
        io.maxTextureSize = 64;
        scene3d::ImportResult imp = scene3d::import_scene_file(a.fbx, io);
        if (!imp.ok()) {
            std::printf("FBX nao abriu: %s\n", imp.detail.c_str());
        } else {
            const rig::HumanoidMap map = rig::humanoid_map(*imp.asset, "");
            rest = rig::rest_pose(*imp.asset, map);
            basis = rig::model_basis(rest);
            std::printf("rig: %u ossos mapeados, usavel %s, altura %.2f\n", map.mapped(), map.usable() ? "sim" : "NAO", rest.height);
            std::vector<rig::PoseFrame> in = frames;
            if (a.solver2d) for (auto& f : in) f.has_aux_world = false;
            rig::MocapParams mp;
            mp.smoothing = a.smooth;
            mp.imageAspect = aspect;
            const auto s0 = std::chrono::steady_clock::now();
            solved = map.usable() && rig::solve(in, rest, mp, result) && result.world.size() == F;
            const auto s1 = std::chrono::steady_clock::now();
            std::printf("solver %s em %.0f ms\n", solved ? "ok" : "FALHOU", std::chrono::duration<double, std::milli>(s1 - s0).count());
        }
    }
    // O solver desfaz a inclinação da câmera (personagem em pé); a reprojeção a refaz.
    const f32 camPitch = solved ? result.report.cameraPitchDeg * 0.01745329252f : 0.0f;
    auto to_camera = [camPitch](Vec3 c) {
        const f32 co = std::cos(camPitch), si = std::sin(camPitch);
        return Vec3{c.x, c.y * co - c.z * si, c.y * si + c.z * co};
    };
    if (solved) {
        std::printf("camera inclinada %.1f graus (desfeito no 3D)\n", result.report.cameraPitchDeg);
        pos.resize(F);
        for (usize f = 0; f < F; ++f) pos[f] = fk(rest, result.world[f]);
        auto cap = [&](Vec3 p) { return to_camera(Vec3{p.dot(basis.left), p.dot(basis.up), p.dot(basis.forward)}); };
        const std::pair<HB, u32> pairs[] = {
            {HB::LeftUpperArm, lm::LeftShoulder}, {HB::LeftLowerArm, lm::LeftElbow}, {HB::LeftHand, lm::LeftWrist},
            {HB::RightUpperArm, lm::RightShoulder}, {HB::RightLowerArm, lm::RightElbow}, {HB::RightHand, lm::RightWrist},
            {HB::LeftUpperLeg, lm::LeftHip}, {HB::LeftLowerLeg, lm::LeftKnee}, {HB::LeftFoot, lm::LeftAnkle},
            {HB::RightUpperLeg, lm::RightHip}, {HB::RightLowerLeg, lm::RightKnee}, {HB::RightFoot, lm::RightAnkle}};
        std::vector<f32> errs;
        for (usize f = 0; f < F; ++f) {
            if (!person(f)) continue;
            std::vector<V2> A, B;
            for (const auto& [b, j] : pairs) {
                if (!rest.present[static_cast<usize>(b)] || frames[f].pts[j].confidence < 0.5f) continue;
                const Vec3 c = cap(pos[f][static_cast<usize>(b)]);
                A.push_back(V2{c.x, c.y});
                B.push_back(V2{frames[f].pts[j].x * aspect, -frames[f].pts[j].y});
            }
            if (A.size() < 4) continue;
            V2 ma, mb;
            for (usize i = 0; i < A.size(); ++i) { ma = ma + A[i]; mb = mb + B[i]; }
            ma = ma * (1.0f / A.size());
            mb = mb * (1.0f / B.size());
            f32 num = 0, den = 0;
            for (usize i = 0; i < A.size(); ++i) {
                const V2 pa = A[i] - ma, pb = B[i] - mb;
                num += pa.x * pb.x + pa.y * pb.y;
                den += pa.x * pa.x + pa.y * pa.y;
            }
            const f32 sc = den > 1e-12f ? num / den : 0.0f;
            const V2 t = mb - ma * sc;
            fit[f] = {sc, t.x, t.y};
            f32 e = 0;
            for (usize i = 0; i < A.size(); ++i) e += len(A[i] * sc + t - B[i]);
            errs.push_back(e / static_cast<f32>(A.size()) / T);
        }
        reproMean = mean(errs);
        reproP95 = pct(errs, 0.95f);
        // Direções dos ossos (espaço de captura) e o tempo.
        const std::pair<HB, HB> bones[] = {
            {HB::LeftUpperArm, HB::LeftLowerArm}, {HB::LeftLowerArm, HB::LeftHand}, {HB::RightUpperArm, HB::RightLowerArm},
            {HB::RightLowerArm, HB::RightHand}, {HB::LeftUpperLeg, HB::LeftLowerLeg}, {HB::LeftLowerLeg, HB::LeftFoot},
            {HB::RightUpperLeg, HB::RightLowerLeg}, {HB::RightLowerLeg, HB::RightFoot}, {HB::Hips, HB::Chest}, {HB::Neck, HB::Head}};
        const std::array<u32, 2> lmBones[] = {{lm::LeftShoulder, lm::LeftElbow}, {lm::LeftElbow, lm::LeftWrist},
                                              {lm::RightShoulder, lm::RightElbow}, {lm::RightElbow, lm::RightWrist},
                                              {lm::LeftHip, lm::LeftKnee}, {lm::LeftKnee, lm::LeftAnkle},
                                              {lm::RightHip, lm::RightKnee}, {lm::RightKnee, lm::RightAnkle}};
        auto dir = [&](usize f, usize k) {
            const auto& [p, c] = bones[k];
            return cap(pos[f][static_cast<usize>(c)] - pos[f][static_cast<usize>(p)]).normalized();
        };
        std::vector<f32> av, aa, aaStill, ag;
        std::array<u32, 10> flipsPerBone{};
        std::string flipLog;
        for (usize k = 0; k < std::size(bones); ++k) {
            if (!rest.present[static_cast<usize>(bones[k].first)] || !rest.present[static_cast<usize>(bones[k].second)]) continue;
            for (usize f = 1; f < F; ++f) {
                if (!person(f) || !person(f - 1)) continue;
                const f32 w = std::acos(std::clamp(dir(f, k).dot(dir(f - 1, k)), -1.0f, 1.0f)) * kToDeg;
                av.push_back(w);
                if (w > 45.0f) { ++flips; ++flipsPerBone[k]; if (flipLog.size() < 400) flipLog += std::to_string(f) + ":" + std::to_string(k) + " "; }
                if (f + 1 < F && person(f + 1)) {
                    const Vec3 d2 = dir(f + 1, k) - dir(f, k) * 2.0f + dir(f - 1, k);
                    const f32 acc = d2.length() * kToDeg;
                    aa.push_back(acc);
                    if (bodySpeed[f] < 0.01f) aaStill.push_back(acc);
                }
            }
            if (k < std::size(lmBones) && worldFrames) {
                for (usize f = 0; f < F; ++f) {
                    if (!person(f) || !frames[f].has_aux_world) continue;
                    const auto& p = frames[f].world[lmBones[k][0]];
                    const auto& q = frames[f].world[lmBones[k][1]];
                    if (p.confidence < 0.5f || q.confidence < 0.5f) continue;
                    const Vec3 wd = Vec3{q.x - p.x, -(q.y - p.y), -(q.z - p.z)}.normalized();
                    ag.push_back(std::acos(std::clamp(wd.dot(dir(f, k)), -1.0f, 1.0f)) * kToDeg);
                }
            }
        }
        // Translação do quadril (em pernas do modelo): tremor parado e o alcance.
        {
            const f32 legUnit = std::max(rig::hips_local_translation(rest, basis.up * result.legLength).length(), 1e-6f);
            std::vector<f32> ra;
            Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
            for (usize f = 1; f + 1 < F && f + 1 < result.hipsLocal.size(); ++f) {
                const Vec3 d = result.hipsLocal[f + 1] - result.hipsLocal[f] * 2.0f + result.hipsLocal[f - 1];
                if (bodySpeed[f] < 0.01f) ra.push_back(d.length() / legUnit);
            }
            for (const Vec3& h : result.hipsLocal) {
                lo = Vec3{std::min(lo.x, h.x), std::min(lo.y, h.y), std::min(lo.z, h.z)};
                hi = Vec3{std::max(hi.x, h.x), std::max(hi.y, h.y), std::max(hi.z, h.z)};
            }
            const Vec3 span = (hi - lo) / legUnit;
            rootAccStill = mean(ra);
            std::printf("RAIZ: tremor parado %.4f pernas/quadro2; alcance local (%.2f, %.2f, %.2f) pernas\n", rootAccStill, span.x, span.y, span.z);
        }
        std::printf("viradas por osso (braco/antebraco E, D, coxa/canela E, D, coluna, cabeca):");
        for (u32 k : flipsPerBone) std::printf(" %u", k);
        std::printf("\n  quadro:osso %s\n", flipLog.c_str());
        angVel = mean(av);
        angVelP95 = pct(av, 0.95f);
        angAcc = mean(aa);
        angAccP95 = pct(aa, 0.95f);
        angAccStill = mean(aaStill);
        agree = mean(ag);
        std::printf("SOLVER: reprojecao media %.3f p95 %.3f (troncos); ossos vel media %.2f p95 %.2f graus/quadro, "
                    "acel media %.2f p95 %.2f (parado %.2f); viradas > 45 graus: %zu; angulo ate o 3D do BlazePose %.1f graus\n",
                    reproMean, reproP95, angVel, angVelP95, angAcc, angAccP95, angAccStill, flips, agree);
    }

    // ---- Folha de contato ------------------------------------------------------------------------
    if (!a.sheet.empty() && !tiles.empty()) {
        const u32 cols = std::min<u32>(8, static_cast<u32>(tiles.size()));
        const u32 rows = static_cast<u32>((tiles.size() + cols - 1) / cols);
        Canvas cv;
        cv.init(cols * (tileW + 4), rows * (kTileH + 4));
        const u8 orange[3]{255, 150, 30}, blue[3]{60, 170, 255}, white[3]{235, 235, 235}, magenta[3]{255, 60, 220},
            green[3]{90, 255, 120}, gray[3]{40, 40, 40};
        const u32 skel[][2] = {{11, 12}, {11, 23}, {12, 24}, {23, 24}, {11, 13}, {13, 15}, {12, 14}, {14, 16}, {23, 25}, {25, 27},
                               {24, 26}, {26, 28}, {27, 29}, {29, 31}, {27, 31}, {28, 30}, {30, 32}, {28, 32}, {15, 19}, {16, 20}};
        auto side = [](u32 j) { return j >= 11 && (j % 2 == 1) ? 0 : j >= 11 ? 1 : 2; };
        for (usize i = 0; i < tiles.size(); ++i) {
            const Tile& t = tiles[i];
            const u32 ox = static_cast<u32>(i % cols) * (tileW + 4), oy = static_cast<u32>(i / cols) * (kTileH + 4);
            cv.blit(t.rgba, tileW, kTileH, ox, oy);
            if (t.frame >= F) continue;
            const rig::PoseFrame& fr = frames[t.frame];
            auto px = [&](u32 j) { return V2{ox + fr.pts[j].x * tileW, oy + fr.pts[j].y * kTileH}; };
            if (person(t.frame)) {
                for (const auto& e : skel) {
                    const u8* c = side(e[0]) == side(e[1]) ? (side(e[0]) == 0 ? orange : blue) : white;
                    const V2 p = px(e[0]), q = px(e[1]);
                    cv.line(p.x, p.y, q.x, q.y, 1.2f, c);
                }
                for (u32 j = 0; j < 33; ++j) {
                    const V2 p = px(j);
                    cv.disc(p.x, p.y, 2.0f, side(j) == 0 ? orange : side(j) == 1 ? blue : white);
                }
            }
            if (solved && person(t.frame) && fit[t.frame][0] > 0.0f) {
                const auto& Pp = pos[t.frame];
                auto cap = [&](Vec3 p) { return to_camera(Vec3{p.dot(basis.left), p.dot(basis.up), p.dot(basis.forward)}); };
                const f32 sc = fit[t.frame][0];
                auto img = [&](HB b) {
                    const Vec3 c = cap(Pp[static_cast<usize>(b)]);
                    const f32 X = sc * c.x + fit[t.frame][1], Y = -(sc * c.y + fit[t.frame][2]);
                    return V2{ox + X / aspect * tileW, oy + Y * kTileH};
                };
                const HB chain[][2] = {{HB::Hips, HB::Spine}, {HB::Spine, HB::Chest}, {HB::Chest, HB::Neck}, {HB::Neck, HB::Head},
                                       {HB::Chest, HB::LeftUpperArm}, {HB::LeftUpperArm, HB::LeftLowerArm}, {HB::LeftLowerArm, HB::LeftHand},
                                       {HB::Chest, HB::RightUpperArm}, {HB::RightUpperArm, HB::RightLowerArm}, {HB::RightLowerArm, HB::RightHand},
                                       {HB::Hips, HB::LeftUpperLeg}, {HB::LeftUpperLeg, HB::LeftLowerLeg}, {HB::LeftLowerLeg, HB::LeftFoot},
                                       {HB::Hips, HB::RightUpperLeg}, {HB::RightUpperLeg, HB::RightLowerLeg}, {HB::RightLowerLeg, HB::RightFoot}};
                for (const auto& e : chain) {
                    if (!rest.present[static_cast<usize>(e[0])] || !rest.present[static_cast<usize>(e[1])]) continue;
                    const V2 p = img(e[0]), q = img(e[1]);
                    cv.line(p.x, p.y, q.x, q.y, 1.0f, magenta, 0.85f);
                }
                // Vista de lado (frente da pessoa para a direita do quadradinho): z × y.
                const f32 box = 0.36f * kTileH;
                const f32 bx = ox + 4, by = oy + kTileH - box - 4;
                for (u32 yy = 0; yy < static_cast<u32>(box); ++yy)
                    for (u32 xx = 0; xx < static_cast<u32>(box); ++xx) cv.dot(static_cast<i32>(bx + xx), static_cast<i32>(by + yy), gray, 0.75f);
                // Vista de lado no referencial NIVELADO (o que o personagem faz na cena).
                auto capL = [&](Vec3 p) { return Vec3{p.dot(basis.left), p.dot(basis.up), p.dot(basis.forward)}; };
                const Vec3 hip = capL(Pp[0]);
                const f32 k2 = box / std::max(rest.height, 1e-3f) * 0.8f;
                auto sv = [&](HB b) {
                    const Vec3 c = capL(Pp[static_cast<usize>(b)]) - hip;
                    return V2{bx + box * 0.5f + c.z * k2, by + box * 0.55f - c.y * k2};
                };
                for (const auto& e : chain) {
                    if (!rest.present[static_cast<usize>(e[0])] || !rest.present[static_cast<usize>(e[1])]) continue;
                    const V2 p = sv(e[0]), q = sv(e[1]);
                    const bool left = e[1] == HB::LeftLowerArm || e[1] == HB::LeftHand || e[1] == HB::LeftLowerLeg || e[1] == HB::LeftFoot;
                    cv.line(p.x, p.y, q.x, q.y, 0.8f, left ? orange : green);
                }
            }
        }
        std::vector<u8> png;
        if (encode_png(cv.px.data(), cv.w, cv.h, false, png).ok()) {
            std::ofstream outf(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(a.sheet.data()), a.sheet.size())),
                               std::ios::binary);
            outf.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
            std::printf("folha de contato: %s (%ux%u, %zu quadros, a cada %zu)\n", a.sheet.c_str(), cv.w, cv.h, tiles.size(), every);
        }
    }
    if (!a.csv.empty()) {
        const bool fresh = !std::filesystem::exists(a.csv);
        std::ofstream c(a.csv, std::ios::app);
        if (fresh)
            c << "label,video,frames,person,lost,detector,swapsFixed,netMs,imgMs,totalMs,stillFrames,accStill,accStillP95,velStill,"
                 "accMove,accMoveP95,wristP95,ankleP95,pops,swapsSeen,worldAccStillMm,worldBoneCv,visWrist,visAnkle,"
                 "reproMean,reproP95,angVel,angVelP95,angAcc,angAccP95,angAccStill,flips,agree3d,rootAccStill\n";
        const std::string vid = std::filesystem::path(a.video).filename().string();
        char line[1024];
        std::snprintf(line, sizeof(line),
                      "%s,%s,%zu,%.0f,%zu,%.0f,%u,%.2f,%.2f,%.2f,%zu,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%zu,%zu,%.3f,%.4f,%.3f,%.3f,"
                      "%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%zu,%.2f,%.5f\n",
                      a.label.c_str(), vid.c_str(), F, state[5], lost, state[4], swaps, state[3], F ? imgMs / F : 0.0, F ? wall / F : 0.0,
                      still, mean(accStill), pct(accStill, 0.95f), mean(velStill), mean(accMove), pct(accMove, 0.95f),
                      pct(accWrist, 0.95f), pct(accAnkle, 0.95f), pops, swapsSeen, worldAccStill, worldBoneCv, visWrist, visAnkle,
                      reproMean, reproP95, angVel, angVelP95, angAcc, angAccP95, angAccStill, flips, agree, rootAccStill);
        c << line;
    }
    bool ordered = true;
    for (usize f = 1; f < F; ++f) ordered &= frames[f].time_s > frames[f - 1].time_s;
    std::printf("finitos %s, tempos crescentes %s\n", finite ? "sim" : "NAO", ordered ? "sim" : "NAO");
    return finite && ordered && F > 0 && state[5] * 2.0f >= static_cast<f32>(F) ? 0 : 1;
}
