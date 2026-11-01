// Contas do painel Esqueleto e dos anéis nas articulações (ver RigSkeleton.hpp).
#include "aurea/scene3d/RigSkeleton.hpp"

#include "aurea/scene3d/SceneAsset.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace aurea::scene3d {
namespace {

char lower(char c) noexcept { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
bool is_upper(char c) noexcept { return std::isupper(static_cast<unsigned char>(c)) != 0; }

/// Primeira ocorrência de `word` (sem caixa) em `s` a partir de `from`.
usize find_word(const std::string& s, const char* word) noexcept {
    const usize n = std::char_traits<char>::length(word);
    if (s.size() < n) return std::string::npos;
    for (usize i = 0; i + n <= s.size(); ++i) {
        usize k = 0;
        while (k < n && lower(s[i + k]) == word[k]) ++k;
        if (k == n) return i;
    }
    return std::string::npos;
}

/// `to` na caixa de `from` (TUDO, Primeira ou minúscula).
std::string cased_like(const std::string& from, const char* to) {
    std::string out(to);
    bool allUpper = !from.empty();
    for (const char c : from) allUpper = allUpper && (!std::isalpha(static_cast<unsigned char>(c)) || is_upper(c));
    if (allUpper && from.size() > 1) {
        for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    } else if (!from.empty() && is_upper(from[0]) && !out.empty()) {
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    }
    return out;
}

bool separator(char c) noexcept { return c == ' ' || c == '_' || c == '.' || c == ':' || c == '-' || c == '|'; }

} // namespace

std::string mirror_bone_name(const std::string& name) {
    // Palavras: a primeira que aparecer decide (Left/Right, esquerdo/direito).
    struct Pair { const char* a; const char* b; };
    static constexpr Pair kWords[] = {{"left", "right"}, {"esquerd", "direit"}};
    usize best = std::string::npos;
    const char* from = nullptr;
    const char* to = nullptr;
    for (const Pair& p : kWords) {
        const usize ia = find_word(name, p.a), ib = find_word(name, p.b);
        if (ia < best) { best = ia; from = p.a; to = p.b; }
        if (ib < best) { best = ib; from = p.b; to = p.a; }
    }
    if (from) {
        const usize n = std::char_traits<char>::length(from);
        return name.substr(0, best) + cased_like(name.substr(best, n), to) + name.substr(best + n);
    }
    // Letra isolada por separador: "Bip01 L Hand", "hand_l", "Hand.L", "L_Hand".
    for (usize i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c != 'L' && c != 'R' && c != 'l' && c != 'r') continue;
        const bool before = i == 0 || separator(name[i - 1]);
        const bool after = i + 1 == name.size() || separator(name[i + 1]);
        if (!before || !after || name.size() == 1) continue;
        std::string out = name;
        out[i] = c == 'L' ? 'R' : c == 'R' ? 'L' : c == 'l' ? 'r' : 'l';
        return out;
    }
    return {};
}

i32 mirror_partner(const SceneAsset& asset, const std::vector<i32>& bones, i32 node) {
    if (node < 0 || static_cast<usize>(node) >= asset.nodes.size()) return -1;
    const std::string want = mirror_bone_name(asset.nodes[static_cast<usize>(node)].name);
    if (want.empty()) return -1;
    i32 loose = -1;
    for (const i32 b : bones) {
        if (b == node || b < 0 || static_cast<usize>(b) >= asset.nodes.size()) continue;
        const std::string& n = asset.nodes[static_cast<usize>(b)].name;
        if (n == want) return b;
        if (loose < 0 && n.size() == want.size()
            && std::equal(n.begin(), n.end(), want.begin(), [](char x, char y) { return lower(x) == lower(y); }))
            loose = b;
    }
    return loose;
}

u32 mirror_axis(const SceneAsset& asset, const std::vector<i32>& bones, const Pose& rest) {
    f32 sum[3]{0, 0, 0};
    for (const i32 b : bones) {
        const i32 p = mirror_partner(asset, bones, b);
        if (p < 0 || static_cast<usize>(b) >= rest.nodeWorld.size() || static_cast<usize>(p) >= rest.nodeWorld.size()) continue;
        const Vec4 a = rest.nodeWorld[static_cast<usize>(b)].col[3], c = rest.nodeWorld[static_cast<usize>(p)].col[3];
        sum[0] += std::fabs(a.x - c.x);
        sum[1] += std::fabs(a.y - c.y);
        sum[2] += std::fabs(a.z - c.z);
    }
    if (!(sum[0] + sum[1] + sum[2] > 0.0f)) return 0;
    return sum[0] >= sum[1] && sum[0] >= sum[2] ? 0u : sum[1] >= sum[2] ? 1u : 2u;
}

Quat mirror_rotation(Quat q, u32 axis) noexcept {
    switch (axis) {
        case 1: return Quat{-q.x, q.y, -q.z, q.w};
        case 2: return Quat{-q.x, -q.y, q.z, q.w};
        default: return Quat{q.x, -q.y, -q.z, q.w};
    }
}

f32 ring_drag_degrees(Vec2 u, Vec2 v, Vec2 p0, Vec2 p1, f32 radius) noexcept {
    constexpr f32 kPi = 3.14159265358979323846f;
    if (!std::isfinite(p0.x) || !std::isfinite(p0.y) || !std::isfinite(p1.x) || !std::isfinite(p1.y)) return 0.0f;
    // Ponto do anel mais perto de onde o dedo estava (amostra + refino local).
    constexpr int kSamples = 96;
    f32 bestT = 0.0f, bestD = 1e30f;
    for (int i = 0; i < kSamples; ++i) {
        const f32 t = 2.0f * kPi * static_cast<f32>(i) / kSamples;
        const Vec2 q = u * std::cos(t) + v * std::sin(t);
        const f32 d = (q - p0).length_sq();
        if (d < bestD) { bestD = d; bestT = t; }
    }
    const Vec2 tangent = v * std::cos(bestT) - u * std::sin(bestT);
    const f32 floor = std::max(0.25f * std::fabs(radius), 1e-3f);
    const f32 denom = std::max(tangent.length_sq(), floor * floor);
    const f32 rad = (p1 - p0).dot(tangent) / denom;
    const f32 deg = rad * 180.0f / kPi;
    return std::isfinite(deg) ? std::clamp(deg, -90.0f, 90.0f) : 0.0f;
}

f32 swept_degrees(Vec2 c, Vec2 p0, Vec2 p1) noexcept {
    const Vec2 a = p0 - c, b = p1 - c;
    if (a.length_sq() < 1e-6f || b.length_sq() < 1e-6f) return 0.0f;
    f32 d = (std::atan2(b.y, b.x) - std::atan2(a.y, a.x)) * 180.0f / 3.14159265358979323846f;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return std::isfinite(d) ? d : 0.0f;
}

} // namespace aurea::scene3d
