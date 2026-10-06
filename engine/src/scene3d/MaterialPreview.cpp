// =============================================================================
//  Aurea / scene3d / MaterialPreview.cpp
//
//  A bola de material das listas do painel 3D (MaterialPreview.hpp).
//
//  Estúdio fixo, pensado para ler o material num ícone de 28 pt:
//   · céu em degradê (claro em cima, chão escuro) — o que o metal reflete;
//   · caixa de luz grande em cima à esquerda (a chave), um recorte atrás à
//     direita (separa a borda do fundo) e um rebatedor fraco embaixo.
//  Difuso = cor × (1 − metal) × irradiância; especular = ambiente na direção
//  do reflexo, com as caixas de luz alargadas pela rugosidade (lobo mais
//  largo e mais fraco: cromado mostra a caixa nítida, fosco só um brilho
//  espalhado), × Fresnel de Schlick com F0 = 0,04 × especular no dielétrico
//  e a cor no metal. Tone map ACES (o mesmo "ombro" suave do grupo 3D) e sRGB.
//
//  O mapa de cor entra como decalque na frente da bola (projeção
//  ortográfica): a imagem inteira cabe no hemisfério visível, centro legível
//  e borda comprimida — dá para reconhecer a textura aplicada numa ficha.
// =============================================================================
#include "aurea/scene3d/MaterialPreview.hpp"

#include "aurea/scene3d/Text3D.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>

namespace aurea::scene3d {
namespace {

f32 saturate(f32 v) noexcept { return std::clamp(v, 0.0f, 1.0f); }
Vec3 mul(Vec3 a, Vec3 b) noexcept { return Vec3{a.x * b.x, a.y * b.y, a.z * b.z}; }
Vec3 mix(Vec3 a, Vec3 b, f32 t) noexcept { return a + (b - a) * t; }

f32 to_linear(u8 c) noexcept { return Color::srgb_to_linear(static_cast<f32>(c) / 255.0f); }

/// Caixa de luz do estúdio: direção, raio angular (rad) e radiância.
struct Softbox {
    Vec3 dir;
    f32  radius;
    Vec3 color;
};

const Softbox kSoftboxes[3] = {
    {Vec3{-0.55f, 0.62f, 0.56f}.normalized(), 0.42f, Vec3{5.0f, 4.88f, 4.65f}},    // chave
    {Vec3{0.86f, 0.24f, -0.45f}.normalized(), 0.30f, Vec3{2.1f, 2.2f, 2.45f}},     // recorte
    {Vec3{0.15f, -0.92f, 0.36f}.normalized(), 0.60f, Vec3{0.22f, 0.22f, 0.22f}},  // rebatedor
};

/// Céu do estúdio na direção `d` (Y para cima): o que o reflexo vê.
Vec3 sky(Vec3 d) noexcept {
    const Vec3 top{0.60f, 0.62f, 0.66f}, horizon{0.30f, 0.30f, 0.31f}, ground{0.05f, 0.05f, 0.055f};
    return d.y >= 0.0f ? mix(horizon, top, std::pow(saturate(d.y), 0.6f))
                       : mix(horizon, ground, std::pow(saturate(-d.y), 0.45f));
}

/// Irradiância difusa (já dividida por π) na normal `n`.
Vec3 irradiance(Vec3 n) noexcept {
    const Vec3 up{0.56f, 0.57f, 0.60f}, down{0.10f, 0.10f, 0.105f};
    Vec3 e = mix(down, up, 0.5f + 0.5f * n.y);
    // Luz pequena de radiância L e raio θ: E/π ≈ L · θ² · cos.
    for (const Softbox& s : kSoftboxes) e += s.color * (s.radius * s.radius * std::max(0.0f, n.dot(s.dir)));
    return e;
}

/// Ambiente refletido na direção `r` com rugosidade perceptual `rough`.
Vec3 reflected(Vec3 r, Vec3 n, f32 rough) noexcept {
    const f32 a = rough * rough;
    // O degradê é de baixa frequência: borrar = puxar para a irradiância.
    Vec3 c = mix(sky(r), irradiance(n) * 0.85f, saturate(rough * 1.1f));
    for (const Softbox& s : kSoftboxes) {
        const f32 spread = s.radius + 1.6f * a + 0.015f;
        const f32 t = std::acos(std::clamp(r.dot(s.dir), -1.0f, 1.0f)) / spread;
        // Expoente alto = borda nítida (cromado); perto de 2 = gaussiana (fosco).
        const f32 edge = 2.0f + 8.0f * (1.0f - saturate(rough * 2.0f));
        const f32 energy = (s.radius / spread) * (s.radius / spread);
        c += s.color * (energy * std::exp(-std::pow(t, edge)));
    }
    return c;
}

/// ACES ajustado (Narkowicz): ombro suave, preto e branco estáveis.
f32 aces(f32 x) noexcept {
    x = std::max(0.0f, x) * 0.8f;
    return saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
}

u8 encode(f32 linear) noexcept {
    return static_cast<u8>(std::lround(saturate(Color::linear_to_srgb(saturate(linear))) * 255.0f));
}

/// Amostra bilinear do mapa (RGBA8 sRGB → linear, alfa linear).
Vec4 sample(const MaterialBall& m, f32 u, f32 v) noexcept {
    const f32 fx = saturate(u) * static_cast<f32>(m.imageWidth) - 0.5f;
    const f32 fy = saturate(v) * static_cast<f32>(m.imageHeight) - 0.5f;
    const i32 x0 = static_cast<i32>(std::floor(fx)), y0 = static_cast<i32>(std::floor(fy));
    const f32 tx = fx - static_cast<f32>(x0), ty = fy - static_cast<f32>(y0);
    const i32 w = static_cast<i32>(m.imageWidth), h = static_cast<i32>(m.imageHeight);
    auto at = [&](i32 x, i32 y) -> Vec4 {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        const u8* p = m.image + (static_cast<usize>(y) * static_cast<usize>(w) + static_cast<usize>(x)) * 4u;
        return Vec4{to_linear(p[0]), to_linear(p[1]), to_linear(p[2]), static_cast<f32>(p[3]) / 255.0f};
    };
    const Vec4 a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
    auto lerp4 = [](const Vec4& p, const Vec4& q, f32 t) {
        return Vec4{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t, p.w + (q.w - p.w) * t};
    };
    return lerp4(lerp4(a, b, tx), lerp4(c, d, tx), ty);
}

bool has_image(const MaterialBall& m) noexcept {
    return m.image && m.imageWidth > 0 && m.imageHeight > 0;
}

// --- Cache por receita ------------------------------------------------------------

u64 fnv(u64 h, const void* data, usize n) noexcept {
    const u8* p = static_cast<const u8*>(data);
    for (usize i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}
template <class T> u64 fnv_of(u64 h, const T& v) noexcept { return fnv(h, &v, sizeof(T)); }

/// Chave da receita: fatores, tamanho e uma amostra em grade do mapa (64×64
/// pontos no máximo — trocar a imagem muda a chave sem ler 16 MB por pedido).
u64 recipe_key(const MaterialBall& m, u32 size) noexcept {
    u64 h = 0xCBF29CE484222325ull;
    h = fnv_of(h, size);
    const f32 f[13]{m.baseColor.x, m.baseColor.y, m.baseColor.z, m.baseColor.w, m.metallic, m.roughness, m.specular,
                    m.emissive.x, m.emissive.y, m.emissive.z, m.alphaCutoff, m.unlit ? 1.0f : 0.0f,
                    static_cast<f32>(static_cast<u8>(m.alphaMode))};
    h = fnv(h, f, sizeof(f));
    if (has_image(m)) {
        h = fnv_of(h, m.imageWidth);
        h = fnv_of(h, m.imageHeight);
        const u32 sx = std::max(1u, m.imageWidth / 64u), sy = std::max(1u, m.imageHeight / 64u);
        for (u32 y = 0; y < m.imageHeight; y += sy)
            for (u32 x = 0; x < m.imageWidth; x += sx)
                h = fnv(h, m.image + (static_cast<usize>(y) * m.imageWidth + x) * 4u, 4);
    }
    return h;
}

struct CachedBall {
    u64 key = 0;
    std::vector<u8> rgba;
};

constexpr usize kCacheBytes = 6u << 20;   // ~170 bolas de 96 px

std::mutex& cache_mutex() { static std::mutex m; return m; }
std::deque<CachedBall>& cache() { static std::deque<CachedBall> c; return c; }

} // namespace

std::vector<u8> render_material_ball(const MaterialBall& m, u32 size) {
    std::vector<u8> out;
    if (size < kMaterialPreviewMinSize || size > kMaterialPreviewMaxSize) return out;
    out.assign(static_cast<usize>(size) * size * 4u, 0);
    const f32 half = static_cast<f32>(size) * 0.5f;
    const f32 radius = half - std::max(1.0f, static_cast<f32>(size) * 0.04f);   // margem para a borda suave
    const Vec3 base{saturate(m.baseColor.x), saturate(m.baseColor.y), saturate(m.baseColor.z)};
    const f32 metallic = saturate(m.metallic);
    const f32 rough = std::clamp(m.roughness, 0.045f, 1.0f);
    const f32 specular = saturate(m.specular);
    const bool image = has_image(m);
    for (u32 py = 0; py < size; ++py) {
        for (u32 px = 0; px < size; ++px) {
            const f32 dx = (static_cast<f32>(px) + 0.5f - half) / radius;
            const f32 dy = (half - static_cast<f32>(py) - 0.5f) / radius;   // Y para cima
            const f32 dist = std::sqrt(dx * dx + dy * dy);
            // Cobertura analítica da borda (1 px de rampa).
            f32 coverage = saturate((1.0f - dist) * radius + 0.5f);
            if (coverage <= 0.0f) continue;
            // Na rampa, sombreia o ponto da borda (normal válida).
            const f32 k = dist > 0.999f ? 0.999f / dist : 1.0f;
            const f32 x = dx * k, y = dy * k;
            const Vec3 n{x, y, std::sqrt(std::max(0.0f, 1.0f - x * x - y * y))};

            Vec3 albedo = base;
            f32 alpha = saturate(m.baseColor.w);
            if (image) {
                const Vec4 t = sample(m, 0.5f + 0.5f * x, 0.5f - 0.5f * y);
                albedo = mul(albedo, Vec3{t.x, t.y, t.z});
                alpha *= t.w;
            }
            if (m.alphaMode == AlphaMode::Mask) alpha = alpha >= m.alphaCutoff ? 1.0f : 0.0f;
            else if (m.alphaMode == AlphaMode::Opaque) alpha = 1.0f;
            coverage *= alpha;
            if (coverage <= 0.0f) continue;

            Vec3 c;
            if (m.unlit) {
                // Unlit = cor de exibição (texto, logotipo): sem luz e sem tone map.
                c = albedo;
            } else {
                const f32 nv = std::max(n.z, 1e-4f);
                const Vec3 r = Vec3{2.0f * nv * n.x, 2.0f * nv * n.y, 2.0f * nv * n.z - 1.0f}.normalized();
                const Vec3 f0 = mix(Vec3{0.04f * specular}, albedo, metallic);
                const f32 f90 = std::max(1.0f - rough, std::max(f0.x, std::max(f0.y, f0.z)));
                const f32 fres = std::pow(1.0f - nv, 5.0f);
                const Vec3 fr = f0 + (Vec3{f90} - f0) * fres;
                const f32 specWeight = metallic + (1.0f - metallic) * specular;
                const Vec3 diffuse = mul(albedo * (1.0f - metallic), irradiance(n)) * (1.0f - fres * specWeight * 0.5f);
                const Vec3 spec = mul(reflected(r, n, rough), fr) * specWeight;
                const Vec3 hdr = diffuse + spec + m.emissive;
                c = Vec3{aces(hdr.x), aces(hdr.y), aces(hdr.z)};
            }
            u8* o = &out[(static_cast<usize>(py) * size + px) * 4u];
            o[0] = encode(c.x);
            o[1] = encode(c.y);
            o[2] = encode(c.z);
            o[3] = static_cast<u8>(std::lround(saturate(coverage) * 255.0f));
        }
    }
    return out;
}

std::vector<u8> material_ball_cached(const MaterialBall& m, u32 size) {
    const u64 key = recipe_key(m, size);
    {
        std::lock_guard<std::mutex> lock(cache_mutex());
        auto& c = cache();
        for (auto it = c.begin(); it != c.end(); ++it) {
            if (it->key != key) continue;
            CachedBall hit = std::move(*it);
            c.erase(it);
            c.push_front(std::move(hit));
            return c.front().rgba;
        }
    }
    std::vector<u8> rgba = render_material_ball(m, size);
    if (rgba.empty()) return rgba;
    std::lock_guard<std::mutex> lock(cache_mutex());
    auto& c = cache();
    c.push_front(CachedBall{key, rgba});
    usize bytes = 0;
    for (const CachedBall& b : c) bytes += b.rgba.size();
    while (bytes > kCacheBytes && c.size() > 1) {
        bytes -= c.back().rgba.size();
        c.pop_back();
    }
    return rgba;
}

MaterialBall material_ball_of(const SceneAsset& asset, const Material& m) noexcept {
    MaterialBall b;
    b.baseColor = m.baseColor;
    b.metallic = m.metallic;
    b.roughness = m.roughness;
    b.specular = m.specular;
    b.emissive = m.emissive * std::max(0.0f, m.emissiveStrength);
    b.alphaMode = m.alphaMode;
    b.alphaCutoff = m.alphaCutoff;
    b.unlit = m.unlit;
    if (m.baseColorTex.valid() && static_cast<usize>(m.baseColorTex.image) < asset.images.size()) {
        const Image& img = asset.images[static_cast<usize>(m.baseColorTex.image)];
        const std::vector<u8>& px = img.pixels();
        if (img.width > 0 && img.height > 0 && px.size() >= static_cast<usize>(img.width) * img.height * 4u) {
            b.image = px.data();
            b.imageWidth = img.width;
            b.imageHeight = img.height;
        }
    }
    return b;
}

bool text3d_preset_ball(u32 preset, MaterialBall& out) noexcept {
    Text3DSpec spec;
    if (!apply_text3d_material_preset(spec, preset)) return false;
    // A mesma conversão do build_text3d (make_material): cor e emissão em sRGB.
    out = MaterialBall{};
    out.baseColor = Vec4{Color::srgb_to_linear(spec.color.x), Color::srgb_to_linear(spec.color.y),
                         Color::srgb_to_linear(spec.color.z), 1.0f};
    out.metallic = saturate(spec.metallic);
    out.roughness = saturate(spec.roughness);
    out.specular = saturate(spec.specular);
    out.emissive = Vec3{Color::srgb_to_linear(spec.emissive.x), Color::srgb_to_linear(spec.emissive.y),
                        Color::srgb_to_linear(spec.emissive.z)} * std::max(0.0f, spec.emissiveStrength);
    return true;
}

} // namespace aurea::scene3d
