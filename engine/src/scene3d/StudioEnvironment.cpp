// =============================================================================
//  Aurea / scene3d / StudioEnvironment.cpp
//
//  Panoramas de estúdio gerados (ver StudioEnvironment.hpp). Convenção do
//  panorama (a mesma do Environment.cpp): Y para cima; u = 0,5 + atan2(x, −z)/2π,
//  v = acos(y)/π. A câmera padrão do Aurea olha para −Z do ambiente (o fundo
//  atrás do modelo fica em u = 0,5); +Z fica atrás da câmera.
//
//  As caixas de luz são RETÂNGULOS no espaço (centro, eixo longo, eixo curto,
//  meias medidas) intersectados pelo raio de cada direção — faixas longas de
//  verdade, não discos: é o que desenha o reflexo comprido na pintura. Borda
//  com rampa curta + superamostragem 2×2 por texel (sem serrilhado no
//  reflexo espelhado). O IBL (pirâmide + GGX) cuida do resto.
// =============================================================================
#include "aurea/scene3d/StudioEnvironment.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace aurea::scene3d {
namespace {

constexpr f32 kPi = 3.14159265358979f;

struct RectLight {
    Vec3 center;      ///< no espaço (a direção do centro, com distância)
    Vec3 axisA;       ///< eixo longo (unitário, perpendicular ao centro)
    Vec3 axisB;       ///< eixo curto
    f32  halfA, halfB;
    f32  edge;        ///< largura da rampa da borda (mesmas unidades)
    Vec3 radiance;    ///< cor × intensidade (linear HDR)
    f32  falloff;     ///< 0 = uniforme; > 0 = centro mais forte (difusor real)
};

f32 sat(f32 v) noexcept { return std::clamp(v, 0.0f, 1.0f); }
f32 smooth(f32 v) noexcept { v = sat(v); return v * v * (3.0f - 2.0f * v); }

Vec3 rect_radiance(const RectLight& r, Vec3 d) noexcept {
    const f32 dist = r.center.length();
    const Vec3 n = r.center * (1.0f / dist);
    const f32 denom = d.dot(n);
    if (denom <= 1e-4f) return Vec3{0, 0, 0};
    const Vec3 p = d * (dist / denom) - r.center;
    const f32 a = std::fabs(p.dot(r.axisA)), b = std::fabs(p.dot(r.axisB));
    if (a >= r.halfA || b >= r.halfB) return Vec3{0, 0, 0};
    const f32 w = smooth((r.halfA - a) / r.edge) * smooth((r.halfB - b) / r.edge);
    const f32 an = a / r.halfA, bn = b / r.halfB;
    const f32 body = 1.0f - r.falloff * (0.6f * an * an + 0.4f * bn * bn);
    return r.radiance * (w * body);
}

// --- 1: estúdio escuro ---------------------------------------------------------
// Faixas de ~0,25 de largura a 3 de distância (~5°): nítidas no espelho,
// finas o bastante para ler como "faixa" e não como "janela".
Vec3 dark_studio(Vec3 d) noexcept {
    static const RectLight lights[] = {
        // Teto: três faixas paralelas correndo de frente para trás.
        {{-1.05f, 3.0f, 0.2f}, {0, 0, 1}, {1, 0, 0}, 2.7f, 0.13f, 0.03f, {16.0f, 16.0f, 16.5f}, 0.25f},
        {{0.0f, 3.1f, 0.2f}, {0, 0, 1}, {1, 0, 0}, 2.9f, 0.16f, 0.03f, {20.0f, 20.0f, 20.5f}, 0.25f},
        {{1.05f, 3.0f, 0.2f}, {0, 0, 1}, {1, 0, 0}, 2.7f, 0.13f, 0.03f, {16.0f, 16.0f, 16.5f}, 0.25f},
        // Faixa horizontal baixa na frente (atrás da câmera): o reflexo
        // comprido ao longo da lateral da carroceria.
        {{0.0f, 0.55f, 3.3f}, {1, 0, 0}, {0, 1, 0}, 2.8f, 0.085f, 0.025f, {9.0f, 9.2f, 9.6f}, 0.15f},
        // Laterais verticais (esquerda/direita, um pouco à frente).
        {{-3.2f, 0.9f, 1.3f}, {0, 1, 0}, {0.376f, 0, 0.927f}, 1.35f, 0.14f, 0.03f, {8.0f, 8.2f, 8.6f}, 0.2f},
        {{3.2f, 0.9f, 1.3f}, {0, 1, 0}, {0.376f, 0, -0.927f}, 1.35f, 0.14f, 0.03f, {8.0f, 8.2f, 8.6f}, 0.2f},
        // Recorte atrás do modelo, alto: contorno do teto e das arestas.
        {{0.0f, 1.5f, -3.2f}, {1, 0, 0}, {0, 0.906f, 0.424f}, 2.6f, 0.12f, 0.03f, {14.0f, 14.2f, 15.0f}, 0.2f},
    };
    // Fundo quase preto com um degradê sutil: mais claro perto do horizonte
    // (parede iluminada de raspão), teto e chão mais escuros.
    const f32 y = d.y;
    Vec3 c;
    if (y >= 0.0f) {
        const f32 t = std::pow(y, 0.6f);
        c = Vec3{0.014f, 0.014f, 0.016f} * (1.0f - t) + Vec3{0.005f, 0.005f, 0.006f} * t;
        // Brilho difuso do teto em volta das faixas (luz que vaza do difusor).
        c = c + Vec3{0.05f, 0.05f, 0.052f} * std::pow(std::max(y, 0.0f), 8.0f);
    } else {
        const f32 t = std::min(1.0f, -y * 3.0f);
        c = Vec3{0.010f, 0.010f, 0.011f} * (1.0f - t) + Vec3{0.003f, 0.003f, 0.003f} * t;
    }
    for (const RectLight& l : lights) c = c + rect_radiance(l, d);
    return c;
}

// --- 2: estúdio de produto ------------------------------------------------------
Vec3 product_studio(Vec3 d) noexcept {
    static const RectLight lights[] = {
        // Softbox grande e macio no alto, um pouco à frente.
        {{0.0f, 3.0f, 0.5f}, {1, 0, 0}, {0, 0.164f, -0.986f}, 1.7f, 1.25f, 0.45f, {7.5f, 7.4f, 7.2f}, 0.35f},
        // Rebatedores laterais largos (verticais).
        {{-3.0f, 0.7f, 0.9f}, {0, 1, 0}, {0.287f, 0, 0.958f}, 1.3f, 0.95f, 0.35f, {2.8f, 2.8f, 2.8f}, 0.3f},
        {{3.0f, 0.7f, 0.9f}, {0, 1, 0}, {0.287f, 0, -0.958f}, 1.3f, 0.95f, 0.35f, {2.4f, 2.4f, 2.45f}, 0.3f},
        // Faixa de recorte atrás, suave.
        {{0.0f, 1.8f, -3.0f}, {1, 0, 0}, {0, 0.857f, 0.514f}, 2.2f, 0.25f, 0.12f, {3.5f, 3.5f, 3.6f}, 0.2f},
    };
    // Ciclorama cinza médio: parede ~0,5, teto um pouco mais claro, chão ~0,4.
    const f32 y = d.y;
    Vec3 c;
    if (y >= 0.0f) {
        const f32 t = std::pow(y, 0.8f);
        c = Vec3{0.50f, 0.50f, 0.51f} * (1.0f - t) + Vec3{0.62f, 0.62f, 0.63f} * t;
    } else {
        const f32 t = smooth(-y * 2.5f);
        c = Vec3{0.50f, 0.50f, 0.51f} * (1.0f - t) + Vec3{0.40f, 0.40f, 0.40f} * t;
    }
    for (const RectLight& l : lights) c = c + rect_radiance(l, d);
    return c;
}

// --- 3: céu e sol -----------------------------------------------------------------
// Unidades: o céu no zênite ≈ 1 (a mesma escala dos estúdios, para a
// exposição do grupo não pular ao trocar de preset). Sol: irradiância do
// disco ≈ 6× a do céu, raio angular 0,6° (maior que o real, 0,27°: um texel
// de um panorama de 1024 tem 0,35° — o disco real sumiria no filtro).
const Vec3 kSunDir = Vec3{-0.45f, 0.62f, 0.64f}.normalized();   // alto, frente-esquerda (atrás da câmera)
constexpr f32 kSunRadius = 0.6f * kPi / 180.0f;

Vec3 sky_sun(Vec3 d) noexcept {
    const f32 y = d.y;
    const f32 cosSun = std::clamp(d.dot(kSunDir), -1.0f, 1.0f);
    const f32 ang = std::acos(cosSun);
    if (y >= 0.0f) {
        const Vec3 zenith{0.16f, 0.36f, 0.95f};
        const Vec3 horizon{0.85f, 0.92f, 1.05f};
        // Névoa perto do horizonte: o azul só domina acima de ~15°.
        const f32 t = std::pow(1.0f - y, 5.0f);
        Vec3 c = zenith * (1.0f - t) + horizon * t;
        // Aureola (espalhamento de Mie) em volta do sol e o céu mais claro
        // do lado dele.
        c = c + Vec3{1.0f, 0.93f, 0.8f} * (1.6f * std::exp(-ang / 0.06f) + 0.25f * std::exp(-ang / 0.5f));
        // Disco do sol com escurecimento de borda.
        if (ang < kSunRadius * 1.15f) {
            const f32 solid = kPi * kSunRadius * kSunRadius;
            // Irradiância ≈ 6·π (céu ≈ π): pico ~55 mil, abaixo do teto do fp16 (65504)
            // — o cubo RGBA16F não pode virar infinito.
            const f32 L = 6.0f * kPi / solid;
            const f32 r = ang / kSunRadius;
            const f32 limb = r < 1.0f ? 1.0f - 0.4f * (1.0f - std::sqrt(std::max(0.0f, 1.0f - r * r))) : 0.0f;
            const f32 edge = smooth((1.15f - r) / 0.15f);
            c = c + Vec3{1.0f, 0.96f, 0.88f} * (L * std::max(limb, 0.0f) * edge);
        }
        return c;
    }
    // Chão: rebatimento difuso (albedo ~0,3 sob céu + sol), desbotando para
    // a névoa do horizonte.
    const Vec3 groundC{0.62f, 0.56f, 0.46f};
    const Vec3 hz{0.70f, 0.74f, 0.80f};
    const f32 t = smooth(-y * 6.0f);
    return hz * (1.0f - t) + groundC * t;
}

Vec3 radiance(u32 preset, Vec3 d) noexcept {
    switch (preset) {
        case 1: return dark_studio(d);
        case 2: return product_studio(d);
        case 3: return sky_sun(d);
        default: return Vec3{0, 0, 0};
    }
}

} // namespace

const char* studio_preset_name(u32 preset) noexcept {
    switch (preset) {
        case 1: return "estudio_escuro";
        case 2: return "estudio_produto";
        case 3: return "ceu_sol";
        default: return nullptr;
    }
}

u32 studio_preset_from_name(const char* name) noexcept {
    if (!name || !*name) return 0;
    for (u32 p = 1; p < kStudioPresetCount; ++p) {
        if (std::strcmp(name, studio_preset_name(p)) == 0) return p;
    }
    if (name[0] >= '1' && name[0] <= '9' && name[1] == 0) {
        const u32 v = static_cast<u32>(name[0] - '0');
        return v < kStudioPresetCount ? v : 0u;
    }
    return 0;
}

std::shared_ptr<HdriPixels> generate_studio_hdri(u32 preset, u32 width) noexcept {
    if (preset == 0 || preset >= kStudioPresetCount) return nullptr;
    width = std::clamp(width, 64u, 4096u) & ~1u;
    const u32 height = width / 2;
    auto px = std::make_shared<HdriPixels>();
    px->width = width;
    px->height = height;
    px->rgb.assign(static_cast<usize>(width) * height * 3, 0.0f);
    // Uma amostra por texel (a rampa das bordas já tem ~2 texels a 2048) e
    // as linhas repartidas entre threads: cada texel é independente, o
    // resultado não depende de quantas.
    auto rows = [&](u32 y0, u32 y1) {
        for (u32 y = y0; y < y1; ++y) {
            const f32 theta = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(height) * kPi;
            const f32 st = std::sin(theta), ct = std::cos(theta);
            for (u32 x = 0; x < width; ++x) {
                const f32 phi = ((static_cast<f32>(x) + 0.5f) / static_cast<f32>(width) - 0.5f) * 2.0f * kPi;
                const Vec3 c = radiance(preset, Vec3{st * std::sin(phi), ct, -st * std::cos(phi)});
                f32* o = &px->rgb[(static_cast<usize>(y) * width + x) * 3];
                o[0] = c.x;
                o[1] = c.y;
                o[2] = c.z;
            }
        }
    };
    const u32 hw = std::max(1u, std::thread::hardware_concurrency());
    const u32 threads = std::clamp(hw > 1 ? hw - 1 : 1u, 1u, 8u);
    std::vector<std::thread> pool;
    const u32 chunk = (height + threads - 1) / threads;
    for (u32 t = 1; t < threads; ++t) {
        const u32 y0 = t * chunk, y1 = std::min(height, y0 + chunk);
        if (y0 < y1) pool.emplace_back(rows, y0, y1);
    }
    rows(0, std::min(height, chunk));
    for (std::thread& th : pool) th.join();
    return px;
}

std::shared_ptr<const HdriPixels> studio_hdri(u32 preset) noexcept {
    if (preset == 0 || preset >= kStudioPresetCount) return nullptr;
    static std::mutex mutex;
    static std::shared_ptr<const HdriPixels> cache[kStudioPresetCount];
    std::lock_guard<std::mutex> lock(mutex);
    if (!cache[preset]) cache[preset] = generate_studio_hdri(preset);
    return cache[preset];
}

} // namespace aurea::scene3d
