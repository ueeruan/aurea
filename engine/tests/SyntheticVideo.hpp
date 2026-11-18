// =============================================================================
//  Vídeo sintético para os testes: um "decoder" que se comporta como H.264.
//
//  Tem GOP (seek cai no keyframe anterior e precisa andar até o alvo), conta
//  seeks e frames descartados, e pode simular custo de decode. Os frames são
//  NV12 de verdade (planos Y e CbCr na memória), então o MESMO caminho de
//  conversão de cor do fallback sem zero-copy roda na GPU do host.
//
//  Padrões:
//    Quadrants   quatro quadrantes vermelho/verde/azul/branco em BT.709
//                limitado — a cor exata que o shader tem de devolver.
//    FrameGray   cinza uniforme cujo nível codifica o índice do frame: o teste
//                sabe QUAL frame foi mostrado lendo um pixel.
// =============================================================================
#pragma once

#include "aurea/media/MediaManager.hpp"
#include "aurea/media/VideoSource.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace aurea::test {

/// PanTable: a textura do PanTexture tirada de uma tabela calculada uma vez,
/// com deslocamento INTEIRO por quadro (⌊5,25·i⌋, ⌊1,75·i⌋). O "decode" em
/// 1080p custa uma cópia em vez de 2 milhões de senos: benchmark de efeito de
/// tempo sem a GPU esfriar esperando a CPU (EffectPerf).
enum class SyntheticPattern : u8 { Quadrants = 0, FrameGray, MovingSquare, FastSquare, Scene3D, PanTexture, PanOcclusion, PanTable, Hero, LiveAction };

/// Cenas de "filmagem real" para o Flow Retime (não rígidas, em cor): o
/// movimento que o anime não tem — membro articulado, rosto girando e
/// mudando de escala, borrão de obturador na fonte, braço passando na frente
/// do rosto e o HUD parado de um jogo sobre a câmera que vira.
enum class LiveKind : u8 { Limb = 0, Face, SourceBlur, OccludingArm, GameHud, Count };
struct LiveScene {
    LiveKind kind = LiveKind::Limb;
    f64 shutter = 0.0;   ///< fração do quadro exposta na fonte (0 = instantâneo)
};

struct HeroScene {
    f64 pan = -16.0;
    f64 radius = 190.0, vx = 46.0, vy = 8.0;
    f64 startX = 380.0, startY = 640.0;
    f64 contrast = 1.0;      ///< amplitude da textura do disco (1 = blocos fortes)
    bool periodic = false;   ///< disco em xadrez: vetor ambíguo, o caso extremo
};

struct SyntheticConfig {
    u32 width = 64;
    u32 height = 36;
    f64 fps = 30.0;
    u32 frameCount = 300;
    u32 gop = 30;                 ///< keyframe a cada N frames
    u32 decodeCostUs = 0;         ///< custo artificial por frame
    SyntheticPattern pattern = SyntheticPattern::Quadrants;
    VideoColorInfo color{};
    u32 rotation = 0;
    /// SyntheticPattern::Hero: fundo em px/quadro e ruído do sensor (níveis de 8 bits).
    HeroScene hero{};
    f64 heroNoise = 2.5;
    i64 heroCutAt = -1;          ///< a partir deste quadro, outra cena (corte seco)
    /// SyntheticPattern::LiveAction (1920×1080): a cena e o ruído do sensor
    /// (`heroNoise`, no Y) valem também aqui.
    LiveScene live{};
    /// Trilha de áudio sintética (0 = sem áudio): senoide por canal, canal c
    /// em audioFreq·(c+1) Hz, amplitude 0,5.
    u32 audioRate = 0;
    u32 audioChannels = 2;
    f64 audioFreq = 440.0;
    f64 audioSeconds = 10.0;
    /// > 0: soma um bumbo (60 Hz decaindo) a cada batida, a partir de
    /// `audioBeatStart` s — para testar a detecção de batidas.
    f64 audioBpm = 0.0;
    f64 audioBeatStart = 0.0;
    /// Decoder preso DENTRO da plataforma ao fechar (MediaCodec ou glFinish
    /// que não volta): os decoders criados com `*holdArm` ligado só terminam o
    /// destrutor quando `*holdClose` desligar.
    std::atomic<bool>* holdArm = nullptr;
    std::atomic<bool>* holdClose = nullptr;
};

/// Centro do quadrado do padrão MovingSquare no quadro `i`.
inline i32 moving_square_x(i64 i) { return 30 + static_cast<i32>(i); }
/// Cena 3D (rastreio de câmera): pontos fixos no mundo filmados por uma câmera
/// que anda e gira (FOV vertical 55°). Mundo → câmera, convenção do Aurea
/// (X direita, Y baixo, Z frente).
struct Scene3DTruth {
    f64 R[9];   // mundo → câmera
    f64 C[3];   // centro
};
inline constexpr f64 kScene3DFov = 55.0;
inline Scene3DTruth scene3d_camera(u32 i, u32 n) {
    const f64 pi = 3.14159265358979323846;
    const f64 u = static_cast<f64>(i) / static_cast<f64>(n > 1 ? n - 1 : 1);
    const f64 ry = (-8.0 + 16.0 * u) * pi / 180.0, rx = 3.0 * std::sin(u * pi) * pi / 180.0, rz = 1.5 * u * pi / 180.0;
    const f64 cx = std::cos(rx), sx = std::sin(rx), cy = std::cos(ry), sy = std::sin(ry), cz = std::cos(rz), sz = std::sin(rz);
    const f64 Rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx}, Ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy}, Rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
    f64 T[9], W[9];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { T[r * 3 + c] = 0; for (int k = 0; k < 3; ++k) T[r * 3 + c] += Ry[r * 3 + k] * Rx[k * 3 + c]; }
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { W[r * 3 + c] = 0; for (int k = 0; k < 3; ++k) W[r * 3 + c] += Rz[r * 3 + k] * T[k * 3 + c]; }
    Scene3DTruth t{};
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) t.R[r * 3 + c] = W[c * 3 + r];
    t.C[0] = -1.2 + 2.4 * u;
    t.C[1] = -0.3 * u;
    t.C[2] = 0.8 * u;
    return t;
}
inline const std::vector<std::array<f64, 3>>& scene3d_points() {
    static const std::vector<std::array<f64, 3>> pts = [] {
        std::vector<std::array<f64, 3>> v;
        u32 seed = 12345u;
        auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return static_cast<f64>(seed >> 8) / static_cast<f64>(1u << 24); };
        for (int i = 0; i < 220; ++i) v.push_back({-5.0 + 10.0 * rnd(), -3.0 + 6.0 * rnd(), 5.0 + 7.0 * rnd()});
        return v;
    }();
    return pts;
}
/// Projeção verdadeira (px) do ponto no quadro i de n; falso se atrás.
inline bool scene3d_project(const std::array<f64, 3>& X, u32 i, u32 n, u32 w, u32 h, f64& u, f64& v) {
    const Scene3DTruth c = scene3d_camera(i, n);
    const f64 f = 0.5 * h / std::tan(0.5 * kScene3DFov * 3.14159265358979323846 / 180.0);
    const f64 d[3] = {X[0] - c.C[0], X[1] - c.C[1], X[2] - c.C[2]};
    const f64 x = c.R[0] * d[0] + c.R[1] * d[1] + c.R[2] * d[2], y = c.R[3] * d[0] + c.R[4] * d[1] + c.R[5] * d[2];
    const f64 z = c.R[6] * d[0] + c.R[7] * d[1] + c.R[8] * d[2];
    if (z < 0.5) return false;
    u = f * x / z + 0.5 * w;
    v = f * y / z + 0.5 * h;
    return true;
}

/// Textura que desliza (qualidade do optical flow, 0.1.8): o valor exato em
/// QUALQUER instante — inclusive entre dois quadros — para comparar o quadro
/// intermediário com a verdade. Senos cruzados + manchas: tem detalhe em toda
/// parte (o Lucas-Kanade precisa de gradiente) sem repetir perto.
inline constexpr f64 kPanTextureVx = 5.25;   ///< px por quadro em x
inline constexpr f64 kPanTextureVy = 1.75;   ///< px por quadro em y
inline f64 pan_texture_value(f64 x, f64 y, f64 frame) {
    const f64 u = x - kPanTextureVx * frame, v = y - kPanTextureVy * frame;
    const f64 a = std::sin(u * 0.21) * std::cos(v * 0.17) + 0.6 * std::sin((u + 2.0 * v) * 0.083)
                + 0.45 * std::cos(std::sqrt(std::fabs(u * u * 0.7 + v * v * 1.3)) * 0.31);
    return std::clamp(128.0 + 52.0 * a, 16.0, 235.0);
}

/// Tabela do padrão PanTable: o PanTexture do quadro 0 numa área que cobre o
/// deslocamento de todos os quadros. Calculada uma vez por tamanho (o decoder
/// roda em outra thread: sob mutex) e nunca solta — é dado de teste.
struct PanTable {
    u32 width = 0, height = 0, shiftX = 0, shiftY = 0;
    std::vector<u8> luma;
};
inline const PanTable& pan_table(u32 w, u32 h, u32 frameCount) {
    static std::mutex m;
    static std::vector<std::unique_ptr<PanTable>> tables;
    std::lock_guard<std::mutex> lock(m);
    const u32 sx = static_cast<u32>(std::ceil(kPanTextureVx * frameCount)) + 1;
    const u32 sy = static_cast<u32>(std::ceil(kPanTextureVy * frameCount)) + 1;
    for (const auto& t : tables)
        if (t->width == w + sx && t->height == h + sy) return *t;
    auto t = std::make_unique<PanTable>();
    t->width = w + sx;
    t->height = h + sy;
    t->shiftX = sx;
    t->shiftY = sy;
    t->luma.resize(static_cast<usize>(t->width) * t->height);
    for (u32 y = 0; y < t->height; ++y)
        for (u32 x = 0; x < t->width; ++x)
            t->luma[static_cast<usize>(y) * t->width + x] = static_cast<u8>(std::lround(
                pan_texture_value(static_cast<f64>(x) - sx, static_cast<f64>(y) - sy, 0.0)));
    tables.push_back(std::move(t));
    return *tables.back();
}

/// Cena em escala de vídeo real (1080p), onde o fluxo trabalha reduzido: céu
/// quase liso (só ruído para o Lucas-Kanade), chão em mosaico de bordas retas
/// e um disco passando por cima. Valor exato em qualquer instante, com a borda
/// filtrada como a de uma câmera.
inline void hero_disc(f64 frame, const HeroScene& h, f64& cx, f64& cy) { cx = h.startX + h.vx * frame; cy = h.startY + h.vy * frame; }
inline f64 hero_point(f64 x, f64 y, f64 frame, const HeroScene& scene) {
    f64 cx, cy;
    hero_disc(frame, scene, cx, cy);
    const f64 dx = x - cx, dy = y - cy;
    if (dx * dx + dy * dy <= scene.radius * scene.radius) {
        const i64 bx = static_cast<i64>(std::floor((dx + 400.0) / 40.0)), by = static_cast<i64>(std::floor((dy + 400.0) / 40.0));
        const u64 cell = (static_cast<u64>(bx * 40503) ^ static_cast<u64>(by * 92821)) * 0x9E3779B97F4A7C15ull;
        const f64 tile = scene.periodic ? (((bx + by) & 1) != 0 ? 205.0 : 55.0) : 45.0 + static_cast<f64>((cell >> 44) % 170u);
        return 130.0 + scene.contrast * (tile - 130.0 + 12.0 * std::sin(dx * 0.09) * std::sin(dy * 0.07));
    }
    const f64 u = x - scene.pan * frame, v = y;
    if (v < 320.0) return 168.0 + 12.0 * std::sin(u * 0.006) + 6.0 * std::sin(u * 0.021 + 1.7) * std::cos(v * 0.013);
    const i64 ix = static_cast<i64>(std::floor(u / 84.0)), iy = static_cast<i64>(std::floor((v - 320.0) / 84.0));
    const u64 h = (static_cast<u64>(ix * 73856093) ^ static_cast<u64>(iy * 19349663)) * 0x9E3779B97F4A7C15ull;
    return 60.0 + static_cast<f64>((h >> 40) % 130u) + 14.0 * std::sin(u * 0.11) * std::cos(v * 0.13);
}
inline f64 hero_value(f64 x, f64 y, f64 frame, const HeroScene& scene) {
    f64 acc = 0.0;
    for (int k = 0; k < 4; ++k)
        acc += hero_point(x + ((k & 1) != 0 ? 0.25 : -0.25), y + ((k & 2) != 0 ? 0.25 : -0.25), frame, scene);
    return std::clamp(acc * 0.25, 16.0, 235.0);
}
/// Ruído de sensor determinístico (triangular, até ±2×amount), novo a cada quadro.
inline f64 hero_noise(u32 x, u32 y, u32 frame, f64 amount) {
    u64 h = (static_cast<u64>(x) * 73856093u) ^ (static_cast<u64>(y) * 19349663u) ^ (static_cast<u64>(frame) * 83492791u);
    h = (h ^ (h >> 13)) * 0x9E3779B97F4A7C15ull;
    return amount * 2.0 * (static_cast<f64>(h >> 56) / 255.0 + static_cast<f64>((h >> 40) & 255u) / 255.0 - 1.0);
}

// --- Cenas de filmagem real (LiveAction) -----------------------------------
// Tudo em R'G'B' 0..1 e em px de 1920×1080; o valor é exato em qualquer
// instante (a verdade do quadro do meio). Bordas com 1 px de transição pela
// distância com sinal, como a de uma câmera.
inline f64 live_hash(i64 x, i64 y, u32 seed) {
    u64 h = (static_cast<u64>(x) * 0x9E3779B97F4A7C15ull) ^ (static_cast<u64>(y) * 0xC2B2AE3D27D4EB4Full)
          ^ (static_cast<u64>(seed) * 0x165667B19E3779F9ull);
    h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32;
    return static_cast<f64>(h >> 11) * (1.0 / 9007199254740992.0);
}
/// Ruído de valor suave (0..1) com período ~1 na unidade da entrada.
inline f64 live_noise(f64 x, f64 y, u32 seed) {
    const f64 fx = std::floor(x), fy = std::floor(y);
    f64 tx = x - fx, ty = y - fy;
    tx = tx * tx * (3.0 - 2.0 * tx);
    ty = ty * ty * (3.0 - 2.0 * ty);
    const i64 ix = static_cast<i64>(fx), iy = static_cast<i64>(fy);
    const f64 a = live_hash(ix, iy, seed), b = live_hash(ix + 1, iy, seed);
    const f64 c = live_hash(ix, iy + 1, seed), d = live_hash(ix + 1, iy + 1, seed);
    return (a + (b - a) * tx) * (1.0 - ty) + (c + (d - c) * tx) * ty;
}
/// Textura "natural": três oitavas (manchas, poros, grão), 0..1.
inline f64 live_fbm(f64 x, f64 y, u32 seed) {
    return 0.5 * live_noise(x / 61.0, y / 61.0, seed) + 0.3 * live_noise(x / 19.0, y / 19.0, seed + 7u)
         + 0.2 * live_noise(x / 6.5, y / 6.5, seed + 13u);
}
inline f64 live_cover(f64 sd) { return std::clamp(0.5 - sd, 0.0, 1.0); }
inline f64 live_capsule(f64 x, f64 y, f64 ax, f64 ay, f64 bx, f64 by, f64 r, f64& along, f64& across) {
    const f64 dx = bx - ax, dy = by - ay, len = std::max(1e-9, std::hypot(dx, dy));
    const f64 ux = dx / len, uy = dy / len;
    along = (x - ax) * ux + (y - ay) * uy;
    across = -(x - ax) * uy + (y - ay) * ux;
    const f64 h = std::clamp(along, 0.0, len);
    return std::hypot(x - (ax + ux * h), y - (ay + uy * h)) - r;
}
inline f64 live_box(f64 x, f64 y, f64 cx, f64 cy, f64 hx, f64 hy, f64 r) {
    const f64 qx = std::fabs(x - cx) - hx + r, qy = std::fabs(y - cy) - hy + r;
    return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - r;
}
inline void live_over(f64 out[3], f64 r, f64 g, f64 b, f64 a) {
    out[0] += (r - out[0]) * a; out[1] += (g - out[1]) * a; out[2] += (b - out[2]) * a;
}
/// Fundo de cena (parede, móveis, janela): manchas de cor + textura + painéis.
inline void live_background(f64 u, f64 v, f64 out[3], bool tealPanel) {
    const f64 big = live_noise(u / 340.0, v / 340.0, 101u), tex = live_fbm(u, v, 202u);
    out[0] = 0.30 + 0.25 * big + 0.22 * (tex - 0.5);
    out[1] = 0.27 + 0.18 * big + 0.20 * (tex - 0.5);
    out[2] = 0.22 + 0.10 * (1.0 - big) + 0.18 * (tex - 0.5);
    // Batentes e quadros: bordas retas em cor (o que um cenário tem).
    const f64 frame1 = live_box(u, v, 300.0, 260.0, 150.0, 110.0, 6.0);
    live_over(out, 0.78, 0.74, 0.62, live_cover(std::fabs(frame1) - 7.0));
    const f64 frame2 = live_box(u, v, 1600.0, 820.0, 210.0, 140.0, 4.0);
    live_over(out, 0.18 + 0.2 * tex, 0.22 + 0.15 * tex, 0.36 + 0.2 * tex, live_cover(frame2));
    if (tealPanel) {
        // Painel quase da mesma luminância da pele (cor muda, luma não): o
        // contorno que só a crominância separa.
        const f64 panel = live_box(u, v, 1060.0, 470.0, 330.0, 330.0, 10.0);
        live_over(out, 0.30 + 0.10 * tex, 0.69 + 0.08 * (tex - 0.5), 0.70 + 0.06 * (tex - 0.5), live_cover(panel));
    }
}
/// Pele com poros (textura baixa: o caso difícil de toda filmagem de gente).
inline void live_skin(f64 lu, f64 lv, f64 out3[3], u32 seed) {
    const f64 t = live_fbm(lu * 1.4, lv * 1.4, seed);
    out3[0] = 0.84 + 0.10 * (t - 0.5); out3[1] = 0.62 + 0.10 * (t - 0.5); out3[2] = 0.50 + 0.08 * (t - 0.5);
}
/// Rosto (elipse com olhos, sobrancelhas, boca e cabelo) em coordenadas locais.
inline void live_face(f64 x, f64 y, f64 cx, f64 cy, f64 angle, f64 scale, f64 out[3]) {
    const f64 c = std::cos(angle), s = std::sin(angle);
    const f64 qx = (c * (x - cx) + s * (y - cy)) / scale, qy = (-s * (x - cx) + c * (y - cy)) / scale;
    const f64 sd = (std::hypot(qx / 180.0, qy / 230.0) - 1.0) * 180.0 * scale;
    const f64 a = live_cover(sd);
    if (a <= 0.0) return;
    f64 col[3];
    live_skin(qx, qy, col, 303u);
    // Cabelo: faixa de cima com fios.
    const f64 hair = qy + 140.0 + 22.0 * std::sin(qx * 0.045);
    const f64 strands = 0.5 + 0.5 * std::sin(qx * 0.55 + 6.0 * live_noise(qx / 40.0, qy / 9.0, 404u));
    const f64 hc = live_cover(hair * scale);
    col[0] += (0.20 + 0.10 * strands - col[0]) * hc;
    col[1] += (0.13 + 0.07 * strands - col[1]) * hc;
    col[2] += (0.08 + 0.05 * strands - col[2]) * hc;
    for (int side = -1; side <= 1; side += 2) {
        const f64 ex = qx - side * 66.0, ey = qy + 40.0;
        const f64 eye = (std::hypot(ex / 34.0, ey / 17.0) - 1.0) * 17.0 * scale;
        const f64 ec = live_cover(eye);
        col[0] += (0.93 - col[0]) * ec; col[1] += (0.92 - col[1]) * ec; col[2] += (0.90 - col[2]) * ec;
        const f64 iris = (std::hypot(ex, ey) - 11.0) * scale;
        const f64 ic = live_cover(iris) * ec;
        col[0] += (0.20 - col[0]) * ic; col[1] += (0.30 - col[1]) * ic; col[2] += (0.18 - col[2]) * ic;
        f64 al, ac;
        const f64 brow = live_capsule(qx, qy, side * 30.0, -78.0, side * 100.0, -84.0, 7.0, al, ac) * scale;
        const f64 bc = live_cover(brow);
        col[0] += (0.22 - col[0]) * bc; col[1] += (0.14 - col[1]) * bc; col[2] += (0.10 - col[2]) * bc;
    }
    f64 al, ac;
    const f64 nose = live_capsule(qx, qy, 0.0, -20.0, 6.0, 40.0, 9.0, al, ac) * scale;
    const f64 nc = live_cover(nose) * 0.45;
    col[0] += (0.66 - col[0]) * nc; col[1] += (0.45 - col[1]) * nc; col[2] += (0.36 - col[2]) * nc;
    const f64 mouth = (std::hypot(qx / 62.0, (qy - 100.0) / 16.0) - 1.0) * 16.0 * scale;
    const f64 mc = live_cover(mouth);
    col[0] += (0.62 - col[0]) * mc; col[1] += (0.22 - col[1]) * mc; col[2] += (0.24 - col[2]) * mc;
    live_over(out, col[0], col[1], col[2], a);
}
/// Valor instantâneo da cena em (x, y) no quadro `t` (R'G'B').
inline void live_instant(f64 x, f64 y, f64 t, LiveKind kind, f64 out[3]) {
    constexpr f64 kDeg = 3.14159265358979323846 / 180.0;
    f64 al, ac;
    switch (kind) {
    case LiveKind::Limb: {
        live_background(x - 3.0 * t, y, out, true);
        // Tronco de camisa e braço em dois segmentos com velocidades angulares
        // diferentes: o campo não é uma translação em lugar nenhum do braço.
        const f64 tx = 2.0 * t;
        const f64 torso = live_box(x, y, 760.0 + tx, 640.0, 150.0, 270.0, 60.0);
        const f64 shirtT = live_fbm(x - tx, y, 505u);
        live_over(out, 0.55 + 0.12 * (shirtT - 0.5), 0.18 + 0.08 * (shirtT - 0.5), 0.20 + 0.08 * (shirtT - 0.5), live_cover(torso));
        const f64 sx = 860.0 + tx, sy = 430.0;
        const f64 a1 = (-30.0 + 5.5 * t) * kDeg, a2 = a1 + (25.0 + 7.0 * t) * kDeg;
        const f64 ex = sx + 250.0 * std::cos(a1), ey = sy + 250.0 * std::sin(a1);
        const f64 wx = ex + 220.0 * std::cos(a2), wy = ey + 220.0 * std::sin(a2);
        const f64 upper = live_capsule(x, y, sx, sy, ex, ey, 36.0, al, ac);
        if (upper < 1.0) {
            // Manga listrada: a listra anda junto com o braço.
            const f64 stripe = 0.5 + 0.5 * std::sin(al * 0.16);
            const f64 ft = live_fbm(al, ac, 606u);
            live_over(out, 0.55 + 0.32 * stripe + 0.08 * (ft - 0.5), 0.18 + 0.60 * stripe, 0.20 + 0.52 * stripe, live_cover(upper));
        }
        const f64 fore = live_capsule(x, y, ex, ey, wx, wy, 30.0, al, ac);
        if (fore < 1.0) { f64 s[3]; live_skin(al, ac, s, 707u); live_over(out, s[0], s[1], s[2], live_cover(fore)); }
        const f64 hx = wx + 38.0 * std::cos(a2), hy = wy + 38.0 * std::sin(a2);
        const f64 hand = std::hypot(x - hx, y - hy) - 42.0;
        if (hand < 1.0) {
            const f64 c = std::cos(a2), s = std::sin(a2);
            f64 sk[3]; live_skin(c * (x - hx) + s * (y - hy), -s * (x - hx) + c * (y - hy), sk, 808u);
            live_over(out, sk[0], sk[1], sk[2], live_cover(hand));
        }
        break;
    }
    case LiveKind::Face: {
        live_background(x - 6.0 * t, y, out, false);
        live_face(x, y, 820.0 + 10.0 * t, 560.0 + 2.0 * t, 4.0 * t * kDeg, 1.0 + 0.015 * t, out);
        break;
    }
    case LiveKind::SourceBlur: {
        live_background(x - 14.0 * t, y, out, false);
        // Corredor: tronco, cabeça, pernas e braços balançando.
        const f64 hx = 300.0 + 38.0 * t, hy = 660.0;
        const f64 nx = hx + 22.0, ny = hy - 260.0;
        const f64 swing = 28.0 * std::sin(2.0 * 3.14159265358979323846 * t / 8.0) * kDeg;
        for (int side = -1; side <= 1; side += 2) {
            const f64 la = 90.0 * kDeg + side * swing;
            const f64 leg = live_capsule(x, y, hx, hy, hx + 300.0 * std::cos(la), hy + 300.0 * std::sin(la), 34.0, al, ac);
            if (leg < 1.0) { const f64 ft = live_fbm(al, ac, 909u + side); live_over(out, 0.16 + 0.1 * ft, 0.17 + 0.1 * ft, 0.24 + 0.12 * ft, live_cover(leg)); }
        }
        const f64 body = live_capsule(x, y, hx, hy, nx, ny, 58.0, al, ac);
        if (body < 1.0) {
            const f64 ft = live_fbm(al, ac, 1001u), logo = live_cover(std::hypot(al - 140.0, ac) - 22.0);
            f64 c[3] = {0.20 + 0.12 * (ft - 0.5), 0.36 + 0.12 * (ft - 0.5), 0.72 + 0.10 * (ft - 0.5)};
            live_over(c, 0.95, 0.85, 0.20, logo);
            live_over(out, c[0], c[1], c[2], live_cover(body));
        }
        for (int side = -1; side <= 1; side += 2) {
            const f64 aa = 90.0 * kDeg - side * swing * 1.2;
            const f64 arm = live_capsule(x, y, nx, ny + 20.0, nx + 230.0 * std::cos(aa), ny + 20.0 + 230.0 * std::sin(aa), 24.0, al, ac);
            if (arm < 1.0) { f64 s[3]; live_skin(al, ac, s, 1101u + side); live_over(out, s[0], s[1], s[2], live_cover(arm)); }
        }
        const f64 head = std::hypot(x - (nx + 8.0), y - (ny - 80.0)) - 58.0;
        if (head < 1.0) {
            f64 s[3]; live_skin(x - nx, y - ny, s, 1201u);
            const f64 hair = live_cover(y - (ny - 100.0));
            live_over(s, 0.12, 0.08, 0.05, 1.0 - hair);
            live_over(out, s[0], s[1], s[2], live_cover(head));
        }
        break;
    }
    case LiveKind::OccludingArm: {
        live_background(x - 4.0 * t, y, out, false);
        live_face(x, y, 1000.0 + 2.0 * t, 520.0, 0.5 * t * kDeg, 1.0, out);
        // Braço da frente varrendo o rosto (pivô fora do quadro).
        const f64 px = 520.0, py = 1420.0, a = (-82.0 + 3.4 * t) * kDeg;
        const f64 tipX = px + 1250.0 * std::cos(a), tipY = py + 1250.0 * std::sin(a);
        const f64 arm = live_capsule(x, y, px, py, tipX, tipY, 72.0, al, ac);
        if (arm < 1.0) {
            const f64 ft = live_fbm(al * 0.8, ac * 0.8, 1301u);
            const f64 seam = live_cover(std::fabs(ac - 20.0) - 3.0);
            f64 c[3] = {0.26 + 0.14 * (ft - 0.5), 0.27 + 0.14 * (ft - 0.5), 0.32 + 0.14 * (ft - 0.5)};
            live_over(c, 0.12, 0.12, 0.15, seam);
            if (al > 1050.0) { f64 s[3]; live_skin(al, ac, s, 1401u); live_over(c, s[0], s[1], s[2], live_cover(1052.0 - al)); }
            live_over(out, c[0], c[1], c[2], live_cover(arm));
        }
        break;
    }
    case LiveKind::GameHud: {
        // Mundo do jogo: a câmera vira (30 px/quadro) e sobe um pouco.
        const f64 u = x - 30.0 * t, v = y - 3.0 * t;
        const f64 tex = live_fbm(u * 1.3, v * 1.3, 1501u);
        out[0] = 0.25 + 0.35 * tex; out[1] = 0.30 + 0.30 * tex; out[2] = 0.20 + 0.25 * tex;
        // Prédios: blocos de borda reta com janelas.
        const f64 cell = std::floor(u / 260.0);
        const f64 height = 250.0 + 380.0 * live_hash(static_cast<i64>(cell), 0, 1601u);
        const f64 bx = u - (cell * 260.0 + 130.0);
        const f64 bsd = std::max(std::fabs(bx) - 105.0, (1080.0 - height) - v);
        const f64 bshade = 0.35 + 0.4 * live_hash(static_cast<i64>(cell), 1, 1601u);
        f64 bc[3] = {bshade, bshade * 0.92, bshade * 0.85};
        const f64 win = live_cover(std::max(std::fabs(std::fmod(bx + 400.0, 36.0) - 18.0) - 8.0, std::fabs(std::fmod(v + 4000.0, 44.0) - 22.0) - 10.0));
        live_over(bc, 0.95, 0.88, 0.55, win * (live_hash(static_cast<i64>(std::floor((bx + 400.0) / 36.0)), static_cast<i64>(std::floor(v / 44.0)), 1701u) > 0.5 ? 1.0 : 0.15));
        live_over(out, bc[0], bc[1], bc[2], live_cover(bsd));
        // Personagem andando no mundo (anda menos que a câmera vira).
        const f64 cx = 1300.0 - 18.0 * t, cy = 760.0;
        const f64 body = live_capsule(x, y, cx, cy, cx, cy - 210.0, 46.0, al, ac);
        if (body < 1.0) { const f64 ft = live_fbm(al, ac, 1801u); live_over(out, 0.55 + 0.2 * ft, 0.12, 0.12, live_cover(body)); }
        const f64 head = std::hypot(x - cx, y - (cy - 280.0)) - 44.0;
        if (head < 1.0) { f64 s[3]; live_skin(x - cx, y - cy, s, 1901u); live_over(out, s[0], s[1], s[2], live_cover(head)); }
        // HUD parado: mira, barra de vida, minimapa e "texto".
        const f64 cross = std::min(live_box(x, y, 960.0, 540.0, 22.0, 2.0, 0.0), live_box(x, y, 960.0, 540.0, 2.0, 22.0, 0.0));
        live_over(out, 0.95, 0.95, 0.95, live_cover(cross));
        live_over(out, 0.08, 0.08, 0.08, live_cover(live_box(x, y, 240.0, 1010.0, 180.0, 22.0, 4.0)) * 0.8);
        live_over(out, 0.85, 0.12, 0.10, live_cover(live_box(x, y, 190.0, 1010.0, 126.0, 15.0, 3.0)));
        const f64 mini = live_box(x, y, 1760.0, 150.0, 120.0, 120.0, 0.0);
        live_over(out, 0.10, 0.18, 0.12, live_cover(mini) * 0.85);
        live_over(out, 0.90, 0.90, 0.90, live_cover(std::fabs(mini) - 2.5));
        for (int k = 0; k < 14; ++k) {
            const f64 gx = 70.0 + k * 19.0 + (k % 4 == 3 ? 12.0 : 0.0);
            live_over(out, 0.97, 0.97, 0.90, live_cover(live_box(x, y, gx, 70.0, 6.0, 11.0 - (k % 3) * 2.0, 1.0)));
        }
        break;
    }
    default: out[0] = out[1] = out[2] = 0.5; break;
    }
}
/// Valor da cena com o obturador da fonte: média em R'G'B' dos instantes
/// dentro de ±shutter/2 (8 amostras) — o borrão de movimento que a câmera grava.
inline void live_value(f64 x, f64 y, f64 t, const LiveScene& scene, f64 out[3]) {
    if (scene.shutter <= 0.0) { live_instant(x, y, t, scene.kind, out); return; }
    constexpr int kSamples = 8;
    out[0] = out[1] = out[2] = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        f64 c[3];
        live_instant(x, y, t + ((i + 0.5) / kSamples - 0.5) * scene.shutter, scene.kind, c);
        out[0] += c[0]; out[1] += c[1]; out[2] += c[2];
    }
    out[0] /= kSamples; out[1] /= kSamples; out[2] /= kSamples;
}

/// Oclusão (o caso difícil do optical flow): o fundo texturizado anda para a
/// esquerda (−3 px/quadro) e um disco com OUTRA textura passa por cima indo
/// para a direita (+9, +2 px/quadro). Borda do disco = pixels que aparecem e
/// somem entre dois quadros. Valor exato em qualquer instante.
inline void pan_occlusion_disc(f64 frame, f64& cx, f64& cy) { cx = 40.0 + 9.0 * frame; cy = 40.0 + 2.0 * frame; }
inline constexpr f64 kPanOcclusionRadius = 22.0;
inline f64 pan_occlusion_value(f64 x, f64 y, f64 frame) {
    f64 cx, cy;
    pan_occlusion_disc(frame, cx, cy);
    const f64 dx = x - cx, dy = y - cy;
    const f64 r = std::sqrt(dx * dx + dy * dy);
    const f64 u = x + 3.0 * frame, v = y;
    const f64 bg = 128.0 + 48.0 * (std::sin(u * 0.19) * std::cos(v * 0.23) + 0.5 * std::sin((u - v) * 0.07));
    const f64 fg = 128.0 + 60.0 * std::sin(dx * 0.17 + 0.4) * std::sin(dy * 0.15 + 1.1) + 18.0 * std::cos((dx - dy) * 0.09);
    // Borda com 1 px de transição (como a câmera faria): sem serrilhado.
    const f64 k = std::clamp(kPanOcclusionRadius + 0.5 - r, 0.0, 1.0);
    return std::clamp(bg * (1.0 - k) + fg * k, 16.0, 235.0);
}

/// Quadrado rápido (optical flow): 8 px por quadro em x, dando a volta.
inline i32 fast_square_x(i64 i) { return 20 + static_cast<i32>((i * 8) % 56); }
inline i32 moving_square_y(i64 i) { return 30 + static_cast<i32>(i / 2); }

/// Valor exato da senoide sintética no instante `t` (s), canal `c`.
inline f32 synthetic_audio_value(const SyntheticConfig& cfg, u32 c, f64 t) {
    constexpr f64 kPi = 3.14159265358979323846;
    if (cfg.audioBpm > 0.0) {
        const f64 period = 60.0 / cfg.audioBpm;
        const f64 since = t - cfg.audioBeatStart;
        const f64 tt = since >= 0.0 ? std::fmod(since, period) : 1e9;
        const f64 kick = tt < 0.2 ? 0.8 * std::exp(-tt * 18.0) * std::sin(2.0 * kPi * 60.0 * tt) : 0.0;
        return static_cast<f32>(0.1 * std::sin(2.0 * kPi * cfg.audioFreq * (c + 1) * t) + kick);
    }
    return static_cast<f32>(0.5 * std::sin(2.0 * kPi * cfg.audioFreq * (c + 1) * t));
}

/// "Decoder" de áudio: entrega quadros de 1024 amostras (como AAC), com pts
/// exato; o seek cai no início do quadro de 1024 anterior ao alvo.
class SyntheticAudioDecoder final : public audio::AudioDecoderBackend {
public:
    explicit SyntheticAudioDecoder(const SyntheticConfig& c) : cfg_(c) {
        info_.sampleRate = c.audioRate;
        info_.channels = c.audioChannels;
        info_.durationUs = static_cast<i64>(c.audioSeconds * 1e6);
        total_ = static_cast<i64>(c.audioSeconds * c.audioRate);
    }
    const audio::AudioStreamInfo& info() const noexcept override { return info_; }
    Status seek(i64 us) noexcept override {
        const i64 f = us * cfg_.audioRate / 1'000'000;
        pos_ = (f / 1024) * 1024;
        ++seeks;
        return OkStatus;
    }
    Status read(std::vector<f32>& out, i64& ptsUs, bool& eos) noexcept override {
        out.clear();
        eos = false;
        if (pos_ >= total_) {
            eos = true;
            return OkStatus;
        }
        const i64 n = std::min<i64>(1024, total_ - pos_);
        out.resize(static_cast<usize>(n) * cfg_.audioChannels);
        for (i64 i = 0; i < n; ++i) {
            const f64 t = static_cast<f64>(pos_ + i) / cfg_.audioRate;
            for (u32 c = 0; c < cfg_.audioChannels; ++c) out[static_cast<usize>(i) * cfg_.audioChannels + c] = synthetic_audio_value(cfg_, c, t);
        }
        ptsUs = pos_ * 1'000'000 / cfg_.audioRate;
        pos_ += n;
        ++chunks;
        return OkStatus;
    }
    u32 seeks = 0;
    u32 chunks = 0;

private:
    SyntheticConfig cfg_;
    audio::AudioStreamInfo info_{};
    i64 total_ = 0;
    i64 pos_ = 0;
};

/// Código Y'CbCr (8 bits) de uma cor R'G'B' codificada, na matriz e faixa dadas.
inline void rgb_to_ycbcr8(f32 r, f32 g, f32 b, const VideoColorInfo& c, u8& y, u8& cb, u8& cr) {
    f32 kr = 0, kb = 0;
    c.coefficients(kr, kb);
    const f32 kg = 1.0f - kr - kb;
    const f32 Y = kr * r + kg * g + kb * b;
    const f32 Cb = (b - Y) / (2.0f * (1.0f - kb));
    const f32 Cr = (r - Y) / (2.0f * (1.0f - kr));
    auto q = [](f32 v) { return static_cast<u8>(std::lround(std::fmin(255.0f, std::fmax(0.0f, v)))); };
    if (c.fullRange) {
        y = q(Y * 255.0f);
        cb = q(Cb * 255.0f + 128.0f);
        cr = q(Cr * 255.0f + 128.0f);
    } else {
        y = q(16.0f + 219.0f * Y);
        cb = q(128.0f + 224.0f * Cb);
        cr = q(128.0f + 224.0f * Cr);
    }
}

/// Nível de cinza (código Y) que o padrão FrameGray usa para o frame `i`.
inline u8 frame_gray_code(u32 i) { return static_cast<u8>(32 + (i * 7) % 190); }

class SyntheticFrame final : public DecodedFrame {
public:
    std::vector<u8> y, uv;
    bool owns_cpu_backing() const noexcept override {
        return !y.empty() && planes[0] == y.data() && planes[1] == uv.data()
            && y.size() + uv.size() >= approx_bytes();
    }
};

class SyntheticDecoder final : public VideoDecoderBackend {
public:
    explicit SyntheticDecoder(const SyntheticConfig& c) : cfg_(c) {
        held_ = c.holdArm && c.holdClose && c.holdArm->load();
        info_.codedWidth = c.width;
        info_.codedHeight = c.height;
        info_.fps = c.fps;
        info_.durationUs = static_cast<i64>(std::llround(static_cast<f64>(c.frameCount) * 1e6 / c.fps));
        info_.color = c.color;
        info_.rotation = c.rotation;
        std::snprintf(info_.codec, sizeof(info_.codec), "%s", "video/sintetico");
        std::snprintf(info_.decoderName, sizeof(info_.decoderName), "%s", "sintetico");
    }

    ~SyntheticDecoder() override {
        while (held_ && cfg_.holdClose->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const VideoStreamInfo& info() const noexcept override { return info_; }

    Status seek_to_keyframe(i64 targetUs) noexcept override {
        const u32 target = frame_of(targetUs);
        pos_ = (target / cfg_.gop) * cfg_.gop;
        seeks.fetch_add(1);
        return OkStatus;
    }

    Status next_frame(i64 deliverFromUs, FrameRef& out, i64& outPtsUs, bool& eos) noexcept override {
        eos = false;
        if (pos_ >= cfg_.frameCount) { eos = true; outPtsUs = pts_of(cfg_.frameCount - 1); return OkStatus; }
        if (cfg_.decodeCostUs) std::this_thread::sleep_for(std::chrono::microseconds(cfg_.decodeCostUs));
        const u32 index = pos_++;
        decoded.fetch_add(1);
        outPtsUs = pts_of(index);
        eos = pos_ >= cfg_.frameCount;
        if (outPtsUs < deliverFromUs) { discarded.fetch_add(1); return OkStatus; }
        out = FrameRef::adopt(make_frame(index));
        delivered.fetch_add(1);
        return OkStatus;
    }

    u32 max_live_frames() const noexcept override { return 12; }
    i64 keyframe_interval_us() const noexcept override {
        return static_cast<i64>(std::llround(static_cast<f64>(cfg_.gop) * 1e6 / cfg_.fps));
    }

    [[nodiscard]] i64 pts_of(u32 index) const {
        return static_cast<i64>(std::llround(static_cast<f64>(index) * 1e6 / cfg_.fps));
    }
    [[nodiscard]] u32 frame_of(i64 us) const {
        const f64 f = static_cast<f64>(us) * cfg_.fps / 1e6 + 0.5;
        const u32 i = f <= 0.0 ? 0u : static_cast<u32>(f);
        return i >= cfg_.frameCount ? cfg_.frameCount - 1 : i;
    }

    std::atomic<u32> seeks{0};
    std::atomic<u32> decoded{0};
    std::atomic<u32> discarded{0};
    std::atomic<u32> delivered{0};

private:
    bool held_ = false;
    DecodedFrame* make_frame(u32 index) {
        auto* f = new SyntheticFrame();
        const u32 w = cfg_.width, h = cfg_.height;
        f->ptsUs = pts_of(index);
        f->width = w;
        f->height = h;
        f->visibleWidth = w;
        f->visibleHeight = h;
        f->rotation = cfg_.rotation;
        f->format = PixelFormat::NV12;
        f->color = cfg_.color;
        f->bufferId = index;
        f->strides[0] = w;
        f->strides[1] = ((w + 1) / 2) * 2;
        f->planeCount = 2;
        if (cfg_.pattern == SyntheticPattern::FrameGray) {
            // Cinza uniforme: os mesmos bytes do laço abaixo, sem o custo por
            // pixel (em 4K o laço levava ~40 ms e virava o gargalo de qualquer
            // benchmark — um decoder de hardware entrega bem mais rápido).
            f->y.assign(static_cast<usize>(w) * h, frame_gray_code(index));
            f->uv.assign(static_cast<usize>((w + 1) / 2) * ((h + 1) / 2) * 2, 128);
            f->planes[0] = f->y.data();
            f->planes[1] = f->uv.data();
            return f;
        }
        if (cfg_.pattern == SyntheticPattern::PanTable) {
            const PanTable& t = pan_table(w, h, cfg_.frameCount);
            // Quadro i = a tabela deslocada (o conteúdo anda para a direita e
            // para baixo, como no PanTexture): u = x − ⌊vx·i⌋.
            const u32 ox = t.shiftX - std::min(t.shiftX, static_cast<u32>(std::floor(kPanTextureVx * index)));
            const u32 oy = t.shiftY - std::min(t.shiftY, static_cast<u32>(std::floor(kPanTextureVy * index)));
            f->y.resize(static_cast<usize>(w) * h);
            for (u32 yy = 0; yy < h; ++yy)
                std::memcpy(&f->y[static_cast<usize>(yy) * w], &t.luma[static_cast<usize>(yy + oy) * t.width + ox], w);
            f->uv.assign(static_cast<usize>((w + 1) / 2) * ((h + 1) / 2) * 2, 128);
            f->planes[0] = f->y.data();
            f->planes[1] = f->uv.data();
            return f;
        }
        if (cfg_.pattern == SyntheticPattern::LiveAction) {
            // Cor de verdade: Y por pixel, Cb/Cr média 2×2 (4:2:0 como a câmera).
            const usize cw = (w + 1) / 2;
            f->y.assign(static_cast<usize>(w) * h, 0);
            f->uv.assign(cw * ((h + 1) / 2) * 2, 128);
            // Em faixas de pares de linhas, em paralelo: a cena com obturador
            // custa 8 avaliações por pixel e o render espera o quadro.
            const u32 pairs = (h + 1) / 2;
            auto band = [&](u32 p0, u32 p1) {
                std::vector<f64> cb(cw, 0.0), cr(cw, 0.0);
                for (u32 pr = p0; pr < p1; ++pr) {
                    std::fill(cb.begin(), cb.end(), 0.0);
                    std::fill(cr.begin(), cr.end(), 0.0);
                    const u32 rows = std::min(2u, h - pr * 2);
                    for (u32 yy = pr * 2; yy < pr * 2 + rows; ++yy)
                        for (u32 xx = 0; xx < w; ++xx) {
                            f64 rgb[3];
                            live_value(xx + 0.5, yy + 0.5, static_cast<f64>(index), cfg_.live, rgb);
                            u8 Y, Cb, Cr;
                            rgb_to_ycbcr8(static_cast<f32>(std::clamp(rgb[0], 0.0, 1.0)), static_cast<f32>(std::clamp(rgb[1], 0.0, 1.0)),
                                          static_cast<f32>(std::clamp(rgb[2], 0.0, 1.0)), cfg_.color, Y, Cb, Cr);
                            f->y[static_cast<usize>(yy) * w + xx] =
                                static_cast<u8>(std::lround(std::clamp(Y + hero_noise(xx, yy, index, cfg_.heroNoise), 16.0, 235.0)));
                            cb[xx / 2] += Cb;
                            cr[xx / 2] += Cr;
                        }
                    const f64 n = rows * 2.0;
                    for (usize cx = 0; cx < cw; ++cx) {
                        const usize ci = (static_cast<usize>(pr) * cw + cx) * 2;
                        f->uv[ci] = static_cast<u8>(std::lround(cb[cx] / n));
                        f->uv[ci + 1] = static_cast<u8>(std::lround(cr[cx] / n));
                    }
                }
            };
            const u32 workers = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
            std::vector<std::thread> pool;
            for (u32 k = 0; k < workers; ++k) pool.emplace_back(band, pairs * k / workers, pairs * (k + 1) / workers);
            for (std::thread& t : pool) t.join();
            f->planes[0] = f->y.data();
            f->planes[1] = f->uv.data();
            return f;
        }
        f->y.assign(static_cast<usize>(w) * h, 0);
        f->uv.assign(static_cast<usize>((w + 1) / 2) * ((h + 1) / 2) * 2, 128);
        // Cena 3D: manchas gaussianas somadas num buffer antes (220 pontos).
        std::vector<f32> scene;
        if (cfg_.pattern == SyntheticPattern::Scene3D) {
            scene.assign(static_cast<usize>(w) * h, 0.0f);
            const auto& pts = scene3d_points();
            for (usize k = 0; k < pts.size(); ++k) {
                f64 u, v;
                if (!scene3d_project(pts[k], index, cfg_.frameCount, w, h, u, v)) continue;
                const f32 amp = 0.35f + 0.55f * static_cast<f32>((k * 37) % 100) / 100.0f;
                for (int dy = -6; dy <= 6; ++dy)
                    for (int dx = -6; dx <= 6; ++dx) {
                        const i32 X = static_cast<i32>(std::floor(u)) + dx, Yy = static_cast<i32>(std::floor(v)) + dy;
                        if (X < 0 || Yy < 0 || X >= static_cast<i32>(w) || Yy >= static_cast<i32>(h)) continue;
                        const f64 ddx = X - u, ddy = Yy - v;
                        scene[static_cast<usize>(Yy) * w + static_cast<usize>(X)] += amp * static_cast<f32>(std::exp(-(ddx * ddx + ddy * ddy) / (2.0 * 1.8 * 1.8)));
                    }
            }
        }
        for (u32 yy = 0; yy < h; ++yy) {
            for (u32 xx = 0; xx < w; ++xx) {
                u8 Y = 0, Cb = 128, Cr = 128;
                if (cfg_.pattern == SyntheticPattern::Scene3D) {
                    Y = static_cast<u8>(std::clamp(30.0f + 200.0f * scene[static_cast<usize>(yy) * w + xx], 16.0f, 235.0f));
                } else if (cfg_.pattern == SyntheticPattern::Hero) {
                    const bool afterCut = cfg_.heroCutAt >= 0 && static_cast<i64>(index) >= cfg_.heroCutAt;
                    const f64 shot = hero_value(xx + (afterCut ? 613.0 : 0.0), yy, static_cast<f64>(index), cfg_.hero);
                    Y = static_cast<u8>(std::lround(std::clamp((afterCut ? 251.0 - shot : shot) +
                                                               hero_noise(xx, yy, static_cast<u32>(index), cfg_.heroNoise), 16.0, 235.0)));
                } else if (cfg_.pattern == SyntheticPattern::PanOcclusion) {
                    Y = static_cast<u8>(std::lround(pan_occlusion_value(static_cast<f64>(xx), static_cast<f64>(yy), static_cast<f64>(index % 16))));
                } else if (cfg_.pattern == SyntheticPattern::PanTexture) {
                    Y = static_cast<u8>(std::lround(pan_texture_value(static_cast<f64>(xx), static_cast<f64>(yy), static_cast<f64>(index))));
                } else if (cfg_.pattern == SyntheticPattern::FrameGray) {
                    Y = frame_gray_code(index);
                } else if (cfg_.pattern == SyntheticPattern::FastSquare) {
                    // Fundo em degradê + quadrado xadrez 21×21 (casas de 7 px)
                    // centrado em fast_square_x, meio da altura.
                    const i32 dx = static_cast<i32>(xx) - fast_square_x(index), dy = static_cast<i32>(yy) - static_cast<i32>(h / 2);
                    Y = static_cast<u8>(100 + (xx * 30) / std::max<u32>(1, w));
                    if (std::abs(dx) <= 10 && std::abs(dy) <= 10) Y = (((dx + 10) / 7 + (dy + 10) / 7) % 2) ? 235 : 20;
                } else if (cfg_.pattern == SyntheticPattern::MovingSquare) {
                    // Fundo em degradê suave + quadrado xadrez 13×13 andando
                    // (1 px por quadro em x, meio em y): alvo de rastreio.
                    const i32 cx = moving_square_x(index), cy = moving_square_y(index);
                    const i32 dx = static_cast<i32>(xx) - cx, dy = static_cast<i32>(yy) - cy;
                    Y = static_cast<u8>(60 + (xx * 40) / std::max<u32>(1, w) + (yy * 30) / std::max<u32>(1, h));
                    if (std::abs(dx) <= 6 && std::abs(dy) <= 6) Y = (((dx + 6) / 3 + (dy + 6) / 3) % 2) ? 235 : 20;
                } else {
                    const bool right = xx >= w / 2, bottom = yy >= h / 2;
                    const f32 r = (!right && !bottom) || (right && bottom) ? 1.0f : 0.0f;
                    const f32 g = (right && !bottom) || (right && bottom) ? 1.0f : 0.0f;
                    const f32 b = (!right && bottom) || (right && bottom) ? 1.0f : 0.0f;
                    rgb_to_ycbcr8(r, g, b, cfg_.color, Y, Cb, Cr);
                }
                f->y[static_cast<usize>(yy) * w + xx] = Y;
                if ((xx % 2 == 0) && (yy % 2 == 0)) {
                    const usize ci = (static_cast<usize>(yy / 2) * ((w + 1) / 2) + xx / 2) * 2;
                    f->uv[ci] = Cb;
                    f->uv[ci + 1] = Cr;
                }
            }
        }
        f->planes[0] = f->y.data();
        f->planes[1] = f->uv.data();
        f->strides[0] = w;
        f->strides[1] = ((w + 1) / 2) * 2;
        f->planeCount = 2;
        return f;
    }

    SyntheticConfig cfg_;
    VideoStreamInfo info_;
    u32 pos_ = 0;
};

/// Fábrica para o MediaManager/Engine: todo asset abre um vídeo sintético com
/// a configuração dada. Guarda o último decoder criado para o teste inspecionar.
class SyntheticFactory final : public VideoSourceFactory {
public:
    explicit SyntheticFactory(const SyntheticConfig& c) : cfg_(c) {}

    bool probe(const char*, MediaProbe& out) override {
        SyntheticDecoder d(cfg_);
        out.video = d.info();
        out.video.color.fromStream = true;
        out.hasVideo = cfg_.width > 0;
        if (cfg_.audioRate > 0) {
            out.hasAudio = true;
            out.audioSampleRate = cfg_.audioRate;
            out.audioChannels = cfg_.audioChannels;
            out.audioDurationUs = static_cast<i64>(cfg_.audioSeconds * 1e6);
        }
        return true;
    }
    std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char*) override {
        if (cfg_.audioRate == 0) return nullptr;
        ++audioOpened;
        return std::make_unique<SyntheticAudioDecoder>(cfg_);
    }
    u32 audioOpened = 0;
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        auto d = std::make_unique<SyntheticDecoder>(cfg_);
        last = d.get();
        ++opened;
        return d;
    }

    std::atomic<SyntheticDecoder*> last{nullptr};
    std::atomic<u32> opened{0};

private:
    SyntheticConfig cfg_;
};

} // namespace aurea::test
