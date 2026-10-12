#include "aurea/effects/Palette.hpp"
#include "aurea/project/FileIO.hpp"

#include "stb_image.h"   // a implementação mora no GltfImporter.cpp

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace aurea {
namespace {
f32 decode(f32 v) noexcept { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }

std::string lower_extension(std::string_view path) {
    const usize dot = path.find_last_of('.');
    const usize slash = path.find_last_of("/\\");
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return {};
    std::string ext(path.substr(dot + 1));
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

Vec3 rgb8(u32 r, u32 g, u32 b) noexcept { return Vec3{r / 255.0f, g / 255.0f, b / 255.0f}; }

/// Uma cor por entrada; repetidas saem (a paleta é um conjunto).
void push_unique(std::vector<Vec3>& out, std::unordered_set<u32>& seen, u32 r, u32 g, u32 b) {
    const u32 key = (r << 16) | (g << 8) | b;
    if (out.size() < kMaxPaletteColors && seen.insert(key).second) out.push_back(rgb8(r, g, b));
}

template <class Line>
void for_each_line(std::string_view text, Line&& line) {
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    while (!text.empty()) {
        usize end = text.find_first_of("\r\n");
        std::string_view l = text.substr(0, end);
        line(l);
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
}

std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}
} // namespace

Vec3 srgb_to_oklab(Vec3 srgb) noexcept {
    const f32 r = decode(std::clamp(srgb.x, 0.0f, 1.0f));
    const f32 g = decode(std::clamp(srgb.y, 0.0f, 1.0f));
    const f32 b = decode(std::clamp(srgb.z, 0.0f, 1.0f));
    const f32 l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    const f32 m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    const f32 s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    return Vec3{0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
                1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
                0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

bool is_palette_path(std::string_view path) noexcept {
    const std::string ext = lower_extension(path);
    return ext == "gpl" || ext == "hex" || ext == "png" || ext == "jpg" || ext == "jpeg";
}

Result<std::vector<Vec3>> parse_gpl_palette(std::string_view text) {
    std::vector<Vec3> out;
    std::unordered_set<u32> seen;
    bool header = false;
    for_each_line(text, [&](std::string_view raw) {
        const std::string_view l = trim(raw);
        if (l.empty() || l.front() == '#') return;
        if (!header) { header = l.starts_with("GIMP Palette"); return; }
        if (l.starts_with("Name:") || l.starts_with("Columns:")) return;
        u32 v[3]{};
        usize pos = 0;
        for (u32& c : v) {
            while (pos < l.size() && std::isspace(static_cast<unsigned char>(l[pos]))) ++pos;
            usize start = pos;
            u32 n = 0;
            while (pos < l.size() && std::isdigit(static_cast<unsigned char>(l[pos])) && pos - start < 4) n = n * 10 + u32(l[pos++] - '0');
            if (pos == start || n > 255) return;
            c = n;
        }
        push_unique(out, seen, v[0], v[1], v[2]);
    });
    if (!header) return Status{Errc::InvalidArgument, "arquivo .gpl sem o cabecalho GIMP Palette"};
    if (out.empty()) return Status{Errc::InvalidArgument, "paleta .gpl sem cores"};
    return out;
}

Result<std::vector<Vec3>> parse_hex_palette(std::string_view text) {
    std::vector<Vec3> out;
    std::unordered_set<u32> seen;
    for_each_line(text, [&](std::string_view raw) {
        std::string_view l = trim(raw);
        if (!l.empty() && l.front() == '#') l.remove_prefix(1);
        if (l.size() < 6) return;
        u32 v[3]{};
        for (u32 i = 0; i < 3; ++i) {
            const int hi = hex_digit(l[i * 2]), lo = hex_digit(l[i * 2 + 1]);
            if (hi < 0 || lo < 0) return;
            v[i] = u32(hi * 16 + lo);
        }
        push_unique(out, seen, v[0], v[1], v[2]);
    });
    if (out.empty()) return Status{Errc::InvalidArgument, "paleta .hex sem cores"};
    return out;
}

Result<std::vector<Vec3>> palette_from_rgba(const u8* rgba, u32 width, u32 height) {
    if (!rgba || !width || !height) return Status{Errc::InvalidArgument, "imagem vazia"};
    std::unordered_map<u32, u32> count;
    std::vector<u32> order;   // ordem de aparição: desempate estável
    const usize n = usize(width) * height;
    for (usize i = 0; i < n; ++i) {
        const u8* p = rgba + i * 4;
        if (p[3] < 128) continue;
        const u32 key = (u32(p[0]) << 16) | (u32(p[1]) << 8) | p[2];
        auto [it, fresh] = count.try_emplace(key, 0u);
        if (fresh) {
            if (count.size() > 1u << 20) return Status{Errc::BudgetExceeded, "imagem com cores demais"};
            order.push_back(key);
        }
        ++it->second;
    }
    if (order.empty()) return Status{Errc::InvalidArgument, "imagem sem pixels visiveis"};
    if (order.size() > kMaxPaletteColors) {
        std::stable_sort(order.begin(), order.end(), [&](u32 a, u32 b) { return count[a] > count[b]; });
        order.resize(kMaxPaletteColors);
    }
    std::vector<Vec3> out;
    out.reserve(order.size());
    for (const u32 key : order) out.push_back(rgb8((key >> 16) & 255u, (key >> 8) & 255u, key & 255u));
    return out;
}

Result<std::vector<Vec3>> read_palette_file(const std::string& path) {
    const std::string ext = lower_extension(path);
    std::vector<u8> bytes;
    if (!fileio::read_all(path, bytes, ext == "gpl" || ext == "hex" ? (1u << 20) : (64u << 20)))
        return Status{Errc::IoError, "nao foi possivel ler a paleta"};
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (ext == "gpl") return parse_gpl_palette(text);
    if (ext == "hex") return parse_hex_palette(text);
    if (ext != "png" && ext != "jpg" && ext != "jpeg") return Status{Errc::UnsupportedFormat, "formato de paleta desconhecido"};
    int w = 0, h = 0, comp = 0;
    if (bytes.empty() || !stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp) || w <= 0 || h <= 0
        || i64(w) * h > 16 * 1024 * 1024)
        return Status{Errc::InvalidArgument, "imagem da paleta invalida ou grande demais"};
    stbi_uc* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp, 4);
    if (!px) return Status{Errc::InvalidArgument, "imagem da paleta invalida"};
    auto colors = palette_from_rgba(px, u32(w), u32(h));
    stbi_image_free(px);
    return colors;
}

CubeLut palette_table(const std::vector<Vec3>& colors) {
    CubeLut t;
    t.dimensions = 0;
    t.size = static_cast<u32>(colors.size());
    t.values.reserve(colors.size() * 2);
    for (const Vec3& c : colors) t.values.push_back(Vec4{c.x, c.y, c.z, 1});
    for (const Vec3& c : colors) { const Vec3 o = srgb_to_oklab(c); t.values.push_back(Vec4{o.x, o.y, o.z, 1}); }
    return t;
}

Result<CubeLut> read_color_table(const std::string& path) {
    if (!is_palette_path(path)) return read_cube_lut(path);
    auto colors = read_palette_file(path);
    if (!colors.ok()) return colors.status();
    return palette_table(*colors);
}
}
