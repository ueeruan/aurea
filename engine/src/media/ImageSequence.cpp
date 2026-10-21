// =============================================================================
//  Aurea / media / ImageSequence.cpp — ver ImageSequence.hpp.
//
//  Receita, detecção do padrão, leitura dos quadros (stb_image e tinyexr; as
//  implementações estão em scene3d/GltfImporter.cpp e third_party/tinyexr) e o
//  decoder com decodificação antecipada.
// =============================================================================
#include "aurea/media/ImageSequence.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/memory/MemoryManager.hpp"
#include "aurea/platform/AddressSpace.hpp"
#include "aurea/project/FileIO.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_map>

#define STBI_NO_STDIO
#include "stb_image.h"
#include "tinyexr.h"

namespace aurea {
namespace {

namespace fs = std::filesystem;

// Tetos de ENTRADA (memória transitória do decode de um quadro).
constexpr u64 kMaxSourceSide = 16384;
constexpr u64 kMax8BitPixels = kAddressSpace32 ? 32ull << 20 : 64ull << 20;    ///< RGBA8: 128/256 MB
constexpr u64 kMaxWidePixels = kAddressSpace32 ? 12ull << 20 : 32ull << 20;    ///< 16 bits / EXR
constexpr usize kMaxFileBytes = kAddressSpace32 ? (256u << 20) : (1024u << 20);
constexpr usize kHeaderProbeBytes = 256u << 10;

std::atomic<u64> gAheadDecodes{0};

std::string utf8_of(const fs::path& p) {
    const auto s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

std::string to_hex(const std::string& s) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) { out += hex[c >> 4]; out += hex[c & 15]; }
    return out;
}

bool from_hex(const std::string& h, std::string& out) {
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (h.size() % 2) return false;
    out.clear();
    out.reserve(h.size() / 2);
    for (usize i = 0; i < h.size(); i += 2) {
        const int a = digit(h[i]), b = digit(h[i + 1]);
        if (a < 0 || b < 0 || (a == 0 && b == 0)) return false;   // sem NUL no caminho
        out += static_cast<char>(a * 16 + b);
    }
    return true;
}

bool parse_i64(const std::string& s, i64& out) {
    if (s.empty() || s.size() > 18) return false;
    i64 v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string extension_of(const std::string& name) {
    const usize dot = name.find_last_of('.');
    return dot == std::string::npos ? std::string{} : lower(name.substr(dot + 1));
}

bool exr_magic(const u8* p, usize n) noexcept {
    return n >= 4 && p[0] == 0x76 && p[1] == 0x2f && p[2] == 0x31 && p[3] == 0x01;
}

// --- Meia precisão --------------------------------------------------------------

/// float → half, arredondamento ao par mais próximo; acima do maior finito fica
/// no maior finito (65504), NaN vira 0 — a textura de trabalho não aceita lixo.
u16 to_half(f32 f) noexcept {
    if (!(f == f)) return 0;
    u32 x;
    std::memcpy(&x, &f, 4);
    const u32 sign = (x >> 16) & 0x8000u;
    const u32 mant = x & 0x7fffffu;
    const i32 exp = static_cast<i32>((x >> 23) & 0xffu) - 127 + 15;
    if (((x >> 23) & 0xffu) == 0xffu) return static_cast<u16>(sign | 0x7bffu);   // ±inf → maior finito
    if (exp >= 31) return static_cast<u16>(sign | 0x7bffu);
    if (exp <= 0) {
        if (exp < -10) return static_cast<u16>(sign);
        const u32 m = mant | 0x800000u;
        const u32 shift = static_cast<u32>(14 - exp);
        u32 h = m >> shift;
        const u32 rem = m & ((1u << shift) - 1u), halfway = 1u << (shift - 1u);
        if (rem > halfway || (rem == halfway && (h & 1u))) ++h;
        return static_cast<u16>(sign | h);
    }
    u32 h = (static_cast<u32>(exp) << 10) | (mant >> 13);
    const u32 rem = mant & 0x1fffu;
    if (rem > 0x1000u || (rem == 0x1000u && (h & 1u))) ++h;
    if ((h & 0x7fffu) >= 0x7c00u) h = 0x7bffu;
    return static_cast<u16>(sign | h);
}

f32 half_value(u16 v) noexcept {
    const u32 sign = (v >> 15) & 1u, exp = (v >> 10) & 31u, man = v & 1023u;
    f32 f;
    if (exp == 0) f = std::ldexp(static_cast<f32>(man), -24);
    else if (exp == 31) f = man ? 0.0f : 65504.0f;
    else f = std::ldexp(static_cast<f32>(man | 1024u), static_cast<int>(exp) - 25);
    return sign ? -f : f;
}

/// u16 (PNG 16 bits, 0..65535) → half de v/65535, por tabela (uma vez só).
const std::array<u16, 65536>& unorm16_half_table() {
    static const std::array<u16, 65536> table = [] {
        std::array<u16, 65536> t{};
        for (u32 i = 0; i < 65536; ++i) t[i] = to_half(static_cast<f32>(i) / 65535.0f);
        return t;
    }();
    return table;
}

// --- Leitura de arquivo ----------------------------------------------------------

bool read_prefix(const std::string& path, std::vector<u8>& out, usize limit, bool* whole) {
    std::FILE* f = fileio::open_file(path, "rb");
    if (!f) return false;
    out.resize(limit);
    const usize n = std::fread(out.data(), 1, limit, f);
    out.resize(n);
    if (whole) *whole = n < limit || std::fgetc(f) == EOF;
    std::fclose(f);
    return n > 0;
}

// --- EXR -------------------------------------------------------------------------

int exr_channel(const EXRHeader& h, char want) {
    // "R" ou "<camada>.R" (a primeira camada que tiver o canal).
    for (int i = 0; i < h.num_channels; ++i) {
        const char* n = h.channels[i].name;
        const usize len = std::strlen(n);
        if (len == 0) continue;
        const char last = static_cast<char>(std::toupper(static_cast<unsigned char>(n[len - 1])));
        if (last == want && (len == 1 || n[len - 2] == '.')) return i;
    }
    return -1;
}

f32 exr_sample(const EXRHeader& h, int channel, usize index, const unsigned char* const* planes) noexcept {
    const unsigned char* plane = planes[channel];
    switch (h.pixel_types[channel]) {
        case TINYEXR_PIXELTYPE_FLOAT: return reinterpret_cast<const float*>(plane)[index];
        case TINYEXR_PIXELTYPE_HALF: return half_value(reinterpret_cast<const u16*>(plane)[index]);
        case TINYEXR_PIXELTYPE_UINT: return static_cast<f32>(reinterpret_cast<const u32*>(plane)[index]);
        default: return 0.0f;
    }
}

struct ExrHeaderGuard {
    EXRHeader header;
    bool parsed = false;
    ExrHeaderGuard() { InitEXRHeader(&header); }
    ~ExrHeaderGuard() { if (parsed) FreeEXRHeader(&header); }
};

Status parse_exr_header(const u8* bytes, usize size, ExrHeaderGuard& g) noexcept {
    EXRVersion version;
    if (ParseEXRVersionFromMemory(&version, bytes, size) != TINYEXR_SUCCESS) return Status{Errc::CorruptData, "EXR ilegivel"};
    if (version.multipart || version.non_image) return Status{Errc::UnsupportedFormat, "EXR multipart/deep nao suportado"};
    const char* err = nullptr;
    if (ParseEXRHeaderFromMemory(&g.header, &version, bytes, size, &err) != TINYEXR_SUCCESS) {
        if (err) FreeEXRErrorMessage(err);
        return Status{Errc::CorruptData, "cabecalho EXR ilegivel"};
    }
    g.parsed = true;
    return OkStatus;
}

/// Janela de exibição do EXR (o quadro), ou a de dados quando a outra é inválida.
void exr_frame_box(const EXRHeader& h, i64& x0, i64& y0, i64& w, i64& hgt) noexcept {
    const i64 dw = static_cast<i64>(h.display_window.max_x) - h.display_window.min_x + 1;
    const i64 dh = static_cast<i64>(h.display_window.max_y) - h.display_window.min_y + 1;
    if (dw > 0 && dh > 0 && static_cast<u64>(dw) <= kMaxSourceSide && static_cast<u64>(dh) <= kMaxSourceSide) {
        x0 = h.display_window.min_x; y0 = h.display_window.min_y; w = dw; hgt = dh;
        return;
    }
    x0 = h.data_window.min_x; y0 = h.data_window.min_y;
    w = static_cast<i64>(h.data_window.max_x) - h.data_window.min_x + 1;
    hgt = static_cast<i64>(h.data_window.max_y) - h.data_window.min_y + 1;
}

f64 exr_fps(const EXRHeader& h) noexcept {
    for (int i = 0; i < h.num_custom_attributes; ++i) {
        const EXRAttribute& a = h.custom_attributes[i];
        if (std::strcmp(a.name, "framesPerSecond") != 0 || std::strcmp(a.type, "rational") != 0 || a.size != 8 || !a.value) continue;
        i32 num = 0; u32 den = 0;
        std::memcpy(&num, a.value, 4);
        std::memcpy(&den, a.value + 4, 4);
        if (num > 0 && den > 0) return static_cast<f64>(num) / static_cast<f64>(den);
    }
    return 0.0;
}

// --- Redução por média de caixa --------------------------------------------------

u32 reduce_factor(u32 w, u32 h) noexcept {
    const u32 side = std::max(w, h);
    return side > kImageSequenceMaxSide ? (side + kImageSequenceMaxSide - 1) / kImageSequenceMaxSide : 1u;
}

/// Saída w/k × h/k; `get(x, y, rgba)` lê o pixel de entrada em float. Alfa reto
/// (`weighted`): a cor é média ponderada pelo alfa, senão os pixels
/// transparentes vazam cor na borda.
template <class Get, class Put>
void box_reduce(u32 w, u32 h, u32 k, bool weighted, Get&& get, Put&& put) {
    const u32 ow = std::max(1u, w / k), oh = std::max(1u, h / k);
    for (u32 oy = 0; oy < oh; ++oy) {
        const u32 y0 = oy * k, y1 = oy + 1 == oh ? h : y0 + k;
        for (u32 ox = 0; ox < ow; ++ox) {
            const u32 x0 = ox * k, x1 = ox + 1 == ow ? w : x0 + k;
            f64 acc[4] = {0, 0, 0, 0};
            f64 alphaSum = 0;
            u32 n = 0;
            for (u32 y = y0; y < y1; ++y)
                for (u32 x = x0; x < x1; ++x) {
                    f32 px[4];
                    get(x, y, px);
                    const f64 wgt = weighted ? px[3] : 1.0;
                    for (u32 c = 0; c < 3; ++c) acc[c] += px[c] * wgt;
                    acc[3] += px[3];
                    alphaSum += wgt;
                    ++n;
                }
            f32 out[4];
            const f64 inv = n ? 1.0 / n : 0.0;
            for (u32 c = 0; c < 3; ++c) out[c] = static_cast<f32>(alphaSum > 0 ? acc[c] / alphaSum : 0.0);
            out[3] = static_cast<f32>(acc[3] * inv);
            put(ox, oy, out);
        }
    }
}

Status decode_stb(const u8* bytes, usize size, bool premultiplied, SequencePixels& out) {
    if (size > static_cast<usize>(std::numeric_limits<int>::max())) return Status{Errc::BudgetExceeded, "quadro grande demais"};
    const int n = static_cast<int>(size);
    int w = 0, h = 0, c = 0;
    if (!stbi_info_from_memory(bytes, n, &w, &h, &c) || w <= 0 || h <= 0) return Status{Errc::UnsupportedFormat, "quadro ilegivel"};
    if (static_cast<u64>(w) > kMaxSourceSide || static_cast<u64>(h) > kMaxSourceSide) return Status{Errc::BudgetExceeded, "quadro grande demais"};
    const bool wide = stbi_is_16_bit_from_memory(bytes, n) != 0;
    const u64 pixels = static_cast<u64>(w) * static_cast<u64>(h);
    if (pixels > (wide ? kMaxWidePixels : kMax8BitPixels)) return Status{Errc::BudgetExceeded, "quadro grande demais"};
    out.hasAlpha = c == 2 || c == 4;
    const u32 W = static_cast<u32>(w), H = static_cast<u32>(h);
    const u32 k = reduce_factor(W, H);
    const bool weighted = out.hasAlpha && !premultiplied;
    if (!wide) {
        stbi_uc* px = stbi_load_from_memory(bytes, n, &w, &h, &c, 4);
        if (!px) return Status{Errc::DecodeFailed, "quadro corrompido"};
        struct Free { stbi_uc* p; ~Free() { stbi_image_free(p); } } guard{px};
        out.format = PixelFormat::RGBA8;
        if (k == 1) {
            out.width = W; out.height = H;
            out.bytes.assign(px, px + pixels * 4);
            return OkStatus;
        }
        out.width = std::max(1u, W / k); out.height = std::max(1u, H / k);
        out.bytes.assign(static_cast<usize>(out.width) * out.height * 4, 0);
        box_reduce(W, H, k, weighted,
            [&](u32 x, u32 y, f32* o) {
                const stbi_uc* s = px + (static_cast<usize>(y) * W + x) * 4;
                for (u32 i = 0; i < 4; ++i) o[i] = s[i] / 255.0f;
            },
            [&](u32 x, u32 y, const f32* v) {
                u8* d = out.bytes.data() + (static_cast<usize>(y) * out.width + x) * 4;
                for (u32 i = 0; i < 4; ++i) d[i] = static_cast<u8>(std::lround(std::clamp(v[i], 0.0f, 1.0f) * 255.0f));
            });
        return OkStatus;
    }
    stbi_us* px = stbi_load_16_from_memory(bytes, n, &w, &h, &c, 4);
    if (!px) return Status{Errc::DecodeFailed, "quadro corrompido"};
    struct Free { stbi_us* p; ~Free() { stbi_image_free(p); } } guard{px};
    out.format = PixelFormat::RGBA16F;
    const auto& table = unorm16_half_table();
    if (k == 1) {
        out.width = W; out.height = H;
        out.bytes.resize(pixels * 8);
        u16* d = reinterpret_cast<u16*>(out.bytes.data());
        for (u64 i = 0; i < pixels * 4; ++i) d[i] = table[px[i]];
        return OkStatus;
    }
    out.width = std::max(1u, W / k); out.height = std::max(1u, H / k);
    out.bytes.assign(static_cast<usize>(out.width) * out.height * 8, 0);
    box_reduce(W, H, k, weighted,
        [&](u32 x, u32 y, f32* o) {
            const stbi_us* s = px + (static_cast<usize>(y) * W + x) * 4;
            for (u32 i = 0; i < 4; ++i) o[i] = s[i] / 65535.0f;
        },
        [&](u32 x, u32 y, const f32* v) {
            u16* d = reinterpret_cast<u16*>(out.bytes.data()) + (static_cast<usize>(y) * out.width + x) * 4;
            for (u32 i = 0; i < 4; ++i) d[i] = to_half(v[i]);
        });
    return OkStatus;
}

Status decode_exr(const u8* bytes, usize size, SequencePixels& out) {
    ExrHeaderGuard g;
    if (const Status s = parse_exr_header(bytes, size, g); !s.ok()) return s;
    const EXRHeader& header = g.header;
    i64 fx = 0, fy = 0, fw = 0, fh = 0;
    exr_frame_box(header, fx, fy, fw, fh);
    const i64 dw = static_cast<i64>(header.data_window.max_x) - header.data_window.min_x + 1;
    const i64 dh = static_cast<i64>(header.data_window.max_y) - header.data_window.min_y + 1;
    if (fw <= 0 || fh <= 0 || dw <= 0 || dh <= 0) return Status{Errc::CorruptData, "EXR sem pixels"};
    if (static_cast<u64>(fw) > kMaxSourceSide || static_cast<u64>(fh) > kMaxSourceSide
        || static_cast<u64>(dw) > kMaxSourceSide || static_cast<u64>(dh) > kMaxSourceSide)
        return Status{Errc::BudgetExceeded, "quadro grande demais"};
    if (static_cast<u64>(fw) * static_cast<u64>(fh) > kMaxWidePixels || static_cast<u64>(dw) * static_cast<u64>(dh) > kMaxWidePixels)
        return Status{Errc::BudgetExceeded, "quadro grande demais"};
    const int cr = exr_channel(header, 'R'), cg = exr_channel(header, 'G'), cb = exr_channel(header, 'B');
    const int cy = exr_channel(header, 'Y'), ca = exr_channel(header, 'A');
    const bool rgb = cr >= 0 && cg >= 0 && cb >= 0;
    if (!rgb && cy < 0) return Status{Errc::UnsupportedFormat, "EXR sem canais RGB ou Y"};
    EXRImage image;
    InitEXRImage(&image);
    const char* err = nullptr;
    if (LoadEXRImageFromMemory(&image, &g.header, bytes, size, &err) != TINYEXR_SUCCESS) {
        if (err) FreeEXRErrorMessage(err);
        return Status{Errc::DecodeFailed, "EXR corrompido"};
    }
    struct ImageGuard { EXRImage* i; ~ImageGuard() { FreeEXRImage(i); } } imageGuard{&image};
    out.hasAlpha = ca >= 0;
    out.format = PixelFormat::RGBA16F;
    const u32 W = static_cast<u32>(fw), H = static_cast<u32>(fh);
    // O quadro inteiro (janela de exibição); fora da janela de dados, transparente.
    std::vector<u16> full(static_cast<usize>(W) * H * 4, 0);
    const i64 ox = static_cast<i64>(header.data_window.min_x) - fx;
    const i64 oy = static_cast<i64>(header.data_window.min_y) - fy;
    const u16 one = to_half(1.0f);
    auto put = [&](const unsigned char* const* planes, usize index, i64 x, i64 y) {
        const i64 X = x + ox, Y = y + oy;
        if (X < 0 || Y < 0 || X >= fw || Y >= fh) return;
        u16* d = full.data() + (static_cast<usize>(Y) * W + static_cast<usize>(X)) * 4;
        if (rgb) {
            d[0] = to_half(exr_sample(header, cr, index, planes));
            d[1] = to_half(exr_sample(header, cg, index, planes));
            d[2] = to_half(exr_sample(header, cb, index, planes));
        } else {
            d[0] = d[1] = d[2] = to_half(exr_sample(header, cy, index, planes));
        }
        d[3] = ca >= 0 ? to_half(std::clamp(exr_sample(header, ca, index, planes), 0.0f, 1.0f)) : one;
    };
    if (image.images) {
        for (i64 y = 0; y < dh; ++y)
            for (i64 x = 0; x < dw; ++x) put(image.images, static_cast<usize>(y) * static_cast<usize>(dw) + static_cast<usize>(x), x, y);
    } else if (image.tiles && image.num_tiles > 0) {
        for (int t = 0; t < image.num_tiles; ++t) {
            const EXRTile& tile = image.tiles[t];
            if (tile.level_x != 0 || tile.level_y != 0 || !tile.images) continue;
            for (int ty = 0; ty < tile.height; ++ty)
                for (int tx = 0; tx < tile.width; ++tx) {
                    const i64 x = static_cast<i64>(tile.offset_x) * header.tile_size_x + tx;
                    const i64 y = static_cast<i64>(tile.offset_y) * header.tile_size_y + ty;
                    if (x < 0 || y < 0 || x >= dw || y >= dh) continue;
                    put(tile.images, static_cast<usize>(ty) * static_cast<usize>(header.tile_size_x) + static_cast<usize>(tx), x, y);
                }
        }
    } else {
        return Status{Errc::CorruptData, "EXR sem pixels"};
    }
    const u32 k = reduce_factor(W, H);
    if (k == 1) {
        out.width = W; out.height = H;
        out.bytes.resize(full.size() * 2);
        std::memcpy(out.bytes.data(), full.data(), out.bytes.size());
        return OkStatus;
    }
    out.width = std::max(1u, W / k); out.height = std::max(1u, H / k);
    out.bytes.assign(static_cast<usize>(out.width) * out.height * 8, 0);
    // EXR é pré-multiplicado: média direta.
    box_reduce(W, H, k, false,
        [&](u32 x, u32 y, f32* o) {
            const u16* s = full.data() + (static_cast<usize>(y) * W + x) * 4;
            for (u32 i = 0; i < 4; ++i) o[i] = half_value(s[i]);
        },
        [&](u32 x, u32 y, const f32* v) {
            u16* d = reinterpret_cast<u16*>(out.bytes.data()) + (static_cast<usize>(y) * out.width + x) * 4;
            for (u32 i = 0; i < 4; ++i) d[i] = to_half(v[i]);
        });
    return OkStatus;
}

// --- Quadro entregue ----------------------------------------------------------------

class SequenceFrame final : public DecodedFrame {
public:
    explicit SequenceFrame(std::shared_ptr<const SequencePixels> pixels) : pixels_(std::move(pixels)) {
        width = pixels_->width;
        height = pixels_->height;
        visibleWidth = width;
        visibleHeight = height;
        format = pixels_->format;
        planes[0] = pixels_->bytes.data();
        strides[0] = pixels_->stride();
        planeCount = 1;
    }
    /// Quadros seguidos do mesmo arquivo (quadro ausente segura o anterior)
    /// dividem os pixels: só o último dono devolve a memória de verdade.
    [[nodiscard]] bool owns_cpu_backing() const noexcept override { return pixels_.use_count() == 1; }

private:
    std::shared_ptr<const SequencePixels> pixels_;
};

// --- Decoder ------------------------------------------------------------------------

class ImageSequenceBackend final : public VideoDecoderBackend {
public:
    ImageSequenceBackend(ImageSequenceSpec spec, std::string directory, MediaPriority priority, MemoryManager* memory,
                         u32 width, u32 height)
        : spec_(std::move(spec)), dir_(std::move(directory)), priority_(priority), memory_(memory) {
        count_ = spec_.frame_count();
        fps_ = spec_.fps > 0.0 && std::isfinite(spec_.fps) ? spec_.fps : 24.0;
        alpha_ = spec_.alpha == SequenceAlphaMode::Premultiplied ? FrameAlpha::Premultiplied
               : spec_.alpha == SequenceAlphaMode::Ignore ? FrameAlpha::Opaque : FrameAlpha::Straight;
        info_.codedWidth = width;
        info_.codedHeight = height;
        info_.fps = fps_;
        info_.durationUs = pts_of(count_);
        info_.color.fullRange = true;
        info_.color.matrix = YCbCrMatrix::BT709;
        info_.color.primaries = ColorPrimaries::BT709;
        info_.color.transfer = spec_.color == SequenceColorMode::Linear ? TransferFunction::Linear : TransferFunction::SRGB;
        info_.color.fromStream = true;
        info_.preciseFrameTiming = true;
        info_.hardwareDecoder = false;
        const std::string ext = extension_of(spec_.suffix);
        std::snprintf(info_.codec, sizeof(info_.codec), "image/%s", ext.empty() ? "sequence" : ext.c_str());
        std::snprintf(info_.decoderName, sizeof(info_.decoderName), "aurea-seq-%s", ext.empty() ? "img" : ext.c_str());
        if (!kAddressSpace32 && priority_ != MediaPriority::Thumbnail) {
            const u32 cores = std::max(1u, std::thread::hardware_concurrency());
            workerCount_ = std::clamp(cores / 2, 1u, 4u);
        } else {
            workerCount_ = priority_ == MediaPriority::Thumbnail ? 0u : 1u;
        }
    }

    ~ImageSequenceBackend() override { stop_workers(); }

    [[nodiscard]] bool valid() const noexcept { return count_ > 0 && !dir_.empty(); }
    [[nodiscard]] const VideoStreamInfo& info() const noexcept override { return info_; }
    [[nodiscard]] u32 max_live_frames() const noexcept override { return 12; }
    /// Acesso direto: andar até um quadro adiante não decodifica os do meio
    /// (next_frame pula), então "andar" nunca custa mais que um seek.
    [[nodiscard]] i64 keyframe_interval_us() const noexcept override { return 2'000'000; }

    [[nodiscard]] Status seek_to_keyframe(i64 targetUs) noexcept override {
        if (count_ <= 0) return Status{Errc::InvalidState, "sequencia vazia"};
        pos_ = index_of(targetUs);
        sequential_ = 0;
        return OkStatus;
    }

    [[nodiscard]] Status next_frame(i64 deliverFromUs, FrameRef& out, i64& outPtsUs, bool& endOfStream) noexcept override {
        endOfStream = false;
        if (count_ <= 0) return Status{Errc::InvalidState, "sequencia vazia"};
        if (pos_ >= count_) {
            endOfStream = true;
            outPtsUs = pts_of(count_ - 1);
            return OkStatus;
        }
        // Quadros antes de `deliverFromUs` não seriam entregues: pula direto
        // para o que contém esse instante, sem decodificar os do meio.
        if (deliverFromUs > pts_of(pos_)) pos_ = std::max(pos_, index_of(deliverFromUs));
        const i64 index = pos_;
        const i64 number = spec_.held_number(spec_.first + index);
        std::shared_ptr<const SequencePixels> px;
        try {
            px = acquire(number);
        } catch (const std::bad_alloc&) {
            return Status{Errc::OutOfMemory, "sem memoria para o quadro da sequencia"};
        }
        if (!px) return Status{Errc::DecodeFailed, "quadro da sequencia ilegivel"};
        auto* frame = new (std::nothrow) SequenceFrame(px);
        if (!frame) return Status{Errc::OutOfMemory, "sem memoria para o quadro da sequencia"};
        frame->ptsUs = pts_of(index);
        frame->durationUs = std::max<i64>(1, pts_of(index + 1) - frame->ptsUs);
        frame->color = info_.color;
        frame->color.bitDepth = px->format == PixelFormat::RGBA16F ? 16 : 8;
        frame->alpha = px->hasAlpha ? alpha_ : FrameAlpha::Opaque;
        frame->bufferId = static_cast<u64>(number);
        out = FrameRef::adopt(frame);
        outPtsUs = frame->ptsUs;
        // Andando para a frente em passos curtos = tocando (ou arrastando para a
        // frente). Atrasado, o pedido pula alguns quadros de cada vez: isso
        // também conta, senão a antecipação desligava justo quando mais falta.
        const bool forward = lastDelivered_ >= 0 && index > lastDelivered_ && index - lastDelivered_ <= kForwardStep;
        sequential_ = forward ? sequential_ + 1 : 0;
        lastDelivered_ = index;
        ++pos_;
        endOfStream = pos_ >= count_;
        // Os próximos arquivos já começam a ser decodificados em paralelo.
        if (sequential_ >= 1 && workerCount_ > 0) {
            try { schedule_ahead(pos_); } catch (...) {}
        }
        return OkStatus;
    }

    void suspend() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = slots_.begin(); it != slots_.end();) {
            if (it->second.state == Slot::Decoding) { ++it; continue; }
            release_slot_locked(it->second);
            it = slots_.erase(it);
        }
    }
    [[nodiscard]] Status resume() noexcept override { return OkStatus; }

private:
    struct Slot {
        enum State : u8 { Queued, Decoding, Ready, Failed } state = Queued;
        std::shared_ptr<const SequencePixels> pixels;
        u64 bytes = 0;
    };

    /// Maior salto adiante que ainda conta como "tocando" (em quadros).
    static constexpr i64 kForwardStep = 8;

    [[nodiscard]] i64 pts_of(i64 index) const noexcept {
        return static_cast<i64>(std::llround(static_cast<f64>(index) * 1e6 / fps_));
    }
    [[nodiscard]] i64 index_of(i64 t) const noexcept {
        if (count_ <= 0 || t <= 0) return 0;
        i64 k = static_cast<i64>(std::floor(static_cast<f64>(t) * fps_ / 1e6));
        k = std::clamp<i64>(k, 0, count_ - 1);
        while (k + 1 < count_ && pts_of(k + 1) <= t) ++k;
        while (k > 0 && pts_of(k) > t) --k;
        return k;
    }

    [[nodiscard]] std::string path_of(i64 number) const {
        return utf8_of(fs::u8path(dir_) / fs::u8path(spec_.file_name(number)));
    }

    std::shared_ptr<const SequencePixels> decode_number(i64 number) {
        auto px = std::make_shared<SequencePixels>();
        const Status s = decode_sequence_frame(path_of(number), *px, spec_.alpha == SequenceAlphaMode::Premultiplied);
        if (!s.ok()) {
            AUREA_LOG_WARN("sequencia: quadro %lld nao abriu (%s)", static_cast<long long>(number), s.message().data());
            return nullptr;
        }
        return px;
    }

    /// Pixels do arquivo `number`: o da antecipação, o recém-usado (quadro
    /// repetido), ou decodificado agora. Arquivo ilegível (apagado depois do
    /// import, corrompido) segura o anterior que abrir.
    std::shared_ptr<const SequencePixels> acquire(i64 number) {
        std::shared_ptr<const SequencePixels> px;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto it = slots_.find(number);
            if (it != slots_.end()) {
                done_.wait(lock, [&] { return it->second.state != Slot::Decoding; });
                if (it->second.state == Slot::Ready) px = it->second.pixels;
                release_slot_locked(it->second);
                slots_.erase(it);
            }
        }
        if (!px && recentNumber_ == number) px = recent_.lock();
        if (!px) px = decode_number(number);
        if (!px) {
            // Segura o anterior: o arquivo presente anterior mais perto que
            // abrir (poucas tentativas) ou, sem nenhum, o recém-usado de antes.
            for (i64 n = number - 1, tries = 0; !px && n >= spec_.first && tries < 8; --n) {
                if (!spec_.has(n)) { n = spec_.held_number(n) + 1; continue; }
                ++tries;
                if (n == recentNumber_) px = recent_.lock();
                if (!px) px = decode_number(n);
            }
            if (!px && recentNumber_ >= 0 && recentNumber_ < number) px = recent_.lock();
            if (!px) return nullptr;
        }
        recent_ = px;
        recentNumber_ = number;
        if (!frameBytes_) frameBytes_ = px->bytes.size();
        return px;
    }

    void release_slot_locked(Slot& slot) noexcept {
        if (slot.bytes && memory_) memory_->free(MemoryClass::DecodedFrames, static_cast<usize>(slot.bytes));
        aheadBytes_ -= std::min(aheadBytes_, slot.bytes);
        slot.bytes = 0;
        slot.pixels.reset();
    }

    /// Quantos quadros adiante cabem: o teto próprio e metade do que resta no
    /// orçamento DecodedFrames (o cache das fontes divide o mesmo orçamento).
    [[nodiscard]] u32 ahead_limit_locked() const noexcept {
        if (!frameBytes_) return 0;
        u64 room = kAddressSpace32 ? (48ull << 20) : (160ull << 20);
        if (memory_) {
            const u64 budget = memory_->budget(MemoryClass::DecodedFrames);
            const u64 used = memory_->used(MemoryClass::DecodedFrames);
            if (budget) {
                const u64 others = used > aheadBytes_ ? used - aheadBytes_ : 0;
                const u64 free = budget > others ? budget - others : 0;
                room = std::min(room, free / 2);
            }
        }
        return static_cast<u32>(std::min<u64>(8, room / frameBytes_));
    }

    void schedule_ahead(i64 from) {
        std::unique_lock<std::mutex> lock(mutex_);
        const u32 limit = ahead_limit_locked();
        std::vector<i64> wanted;
        for (i64 i = from; i < count_ && wanted.size() < limit && i < from + 2 * static_cast<i64>(limit) + 2; ++i) {
            const i64 n = spec_.held_number(spec_.first + i);
            if (std::find(wanted.begin(), wanted.end(), n) == wanted.end()) wanted.push_back(n);
        }
        // Fora da janela nova: solta (os que ainda decodificam saem depois).
        for (auto it = slots_.begin(); it != slots_.end();) {
            if (it->second.state != Slot::Decoding && std::find(wanted.begin(), wanted.end(), it->first) == wanted.end()) {
                release_slot_locked(it->second);
                it = slots_.erase(it);
            } else ++it;
        }
        bool added = false;
        for (i64 n : wanted) {
            if (n == recentNumber_ && !recent_.expired()) continue;
            if (slots_.emplace(n, Slot{}).second) added = true;
        }
        if (!added) return;
        if (workers_.empty()) {
            stop_ = false;
            for (u32 i = 0; i < workerCount_; ++i) workers_.emplace_back([this] { worker_main(); });
        }
        lock.unlock();
        work_.notify_all();
    }

    void worker_main() noexcept {
        set_current_thread_name("aurea-seq");
        set_current_thread_priority(ThreadPriority::Decode);
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            auto queued = slots_.end();
            work_.wait(lock, [&] {
                if (stop_) return true;
                queued = std::find_if(slots_.begin(), slots_.end(), [](const auto& s) { return s.second.state == Slot::Queued; });
                return queued != slots_.end();
            });
            if (stop_) return;
            const i64 number = queued->first;
            queued->second.state = Slot::Decoding;
            lock.unlock();
            std::shared_ptr<const SequencePixels> px;
            try { px = decode_number(number); } catch (...) { px.reset(); }
            lock.lock();
            auto it = slots_.find(number);
            if (it != slots_.end()) {
                it->second.state = px ? Slot::Ready : Slot::Failed;
                it->second.pixels = std::move(px);
                if (it->second.pixels) {
                    it->second.bytes = it->second.pixels->bytes.size();
                    aheadBytes_ += it->second.bytes;
                    if (memory_) memory_->commit(MemoryClass::DecodedFrames, static_cast<usize>(it->second.bytes));
                    gAheadDecodes.fetch_add(1, std::memory_order_relaxed);
                }
            }
            done_.notify_all();
        }
    }

    void stop_workers() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        work_.notify_all();
        for (std::thread& t : workers_) if (t.joinable()) t.join();
        workers_.clear();
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [n, slot] : slots_) release_slot_locked(slot);
        slots_.clear();
    }

    ImageSequenceSpec spec_;
    std::string dir_;
    MediaPriority priority_;
    MemoryManager* memory_ = nullptr;
    VideoStreamInfo info_{};
    i64 count_ = 0;
    f64 fps_ = 24.0;
    FrameAlpha alpha_ = FrameAlpha::Straight;
    u32 workerCount_ = 0;

    // Só a thread de decode da fonte (VideoSource) toca.
    i64 pos_ = 0;
    i64 lastDelivered_ = -2;
    u32 sequential_ = 0;
    std::weak_ptr<const SequencePixels> recent_;   ///< fraco: não segura memória além do cache
    i64 recentNumber_ = -1;
    u64 frameBytes_ = 0;

    // Antecipação.
    std::mutex mutex_;
    std::condition_variable work_, done_;
    std::map<i64, Slot> slots_;   ///< por número de arquivo
    std::vector<std::thread> workers_;
    bool stop_ = false;
    u64 aheadBytes_ = 0;          ///< contados em DecodedFrames enquanto esperam
};

} // namespace

// =============================================================================
// Receita
// =============================================================================

bool ImageSequenceSpec::has(i64 number) const noexcept {
    if (number < first || number > last) return false;
    if (present.empty()) return true;
    auto it = std::upper_bound(present.begin(), present.end(), number,
        [](i64 v, const std::pair<i64, i64>& r) { return v < r.first; });
    if (it == present.begin()) return false;
    --it;
    return number <= it->second;
}

i64 ImageSequenceSpec::held_number(i64 number) const noexcept {
    number = std::clamp(number, first, std::max(first, last));
    if (present.empty()) return number;
    auto it = std::upper_bound(present.begin(), present.end(), number,
        [](i64 v, const std::pair<i64, i64>& r) { return v < r.first; });
    if (it == present.begin()) return present.front().first;
    --it;
    return std::min(number, it->second);
}

std::string ImageSequenceSpec::file_name(i64 number) const {
    std::string digits = std::to_string(std::max<i64>(0, number));
    if (digits.size() < padding) digits.insert(0, padding - digits.size(), '0');
    return prefix + digits + suffix;
}

std::vector<i64> ImageSequenceSpec::present_numbers() const {
    std::vector<i64> out;
    if (present.empty()) {
        for (i64 n = first; n <= last; ++n) out.push_back(n);
        return out;
    }
    for (const auto& [a, b] : present)
        for (i64 n = a; n <= b; ++n) out.push_back(n);
    return out;
}

u64 ImageSequenceSpec::present_count() const noexcept {
    if (present.empty()) return static_cast<u64>(frame_count());
    u64 n = 0;
    for (const auto& [a, b] : present) n += static_cast<u64>(b - a + 1);
    return n;
}

std::string ImageSequenceSpec::display_pattern() const {
    return prefix + std::string(std::max<u32>(1, padding), '#') + suffix;
}

bool is_image_sequence(const std::string& sourcePath) noexcept {
    return sourcePath.rfind(kImageSequenceScheme, 0) == 0;
}

std::string encode_image_sequence(const ImageSequenceSpec& s) {
    std::string out = kImageSequenceScheme;
    out += "v1;d=" + to_hex(s.directory) + ";p=" + to_hex(s.prefix) + ";s=" + to_hex(s.suffix) + ";";
    out += "z=" + std::to_string(s.padding) + ";a=" + std::to_string(s.first) + ";b=" + std::to_string(s.last) + ";";
    const bool full = s.present.empty() || (s.present.size() == 1 && s.present[0].first == s.first && s.present[0].second == s.last);
    if (!full) {
        out += "n=";
        for (usize i = 0; i < s.present.size(); ++i) {
            if (i) out += ',';
            out += std::to_string(s.present[i].first);
            if (s.present[i].second != s.present[i].first) out += "-" + std::to_string(s.present[i].second);
        }
        out += ';';
    }
    char fps[48];
    std::snprintf(fps, sizeof(fps), "r=%.17g;", s.fps);
    out += fps;
    const u32 alpha = s.alpha == SequenceAlphaMode::Ignore ? 0u : s.alpha == SequenceAlphaMode::Premultiplied ? 2u : 1u;
    out += "al=" + std::to_string(alpha) + ";c=" + std::string(s.color == SequenceColorMode::Linear ? "1" : "0") + ";m=h;";
    return out;
}

bool decode_image_sequence(const std::string& src, ImageSequenceSpec& out) {
    const std::string scheme = kImageSequenceScheme;
    if (src.rfind(scheme, 0) != 0) return false;
    ImageSequenceSpec s;
    bool haveDir = false, haveSuffix = false, haveFirst = false, haveLast = false, haveVersion = false;
    usize i = scheme.size();
    while (i < src.size()) {
        usize j = src.find(';', i);
        if (j == std::string::npos) j = src.size();
        const std::string kv = src.substr(i, j - i);
        i = j + 1;
        if (kv == "v1") { haveVersion = true; continue; }
        const usize eq = kv.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        const std::string key = kv.substr(0, eq), value = kv.substr(eq + 1);
        if (key == "d") haveDir = from_hex(value, s.directory) && !s.directory.empty();
        else if (key == "p") { if (!from_hex(value, s.prefix)) return false; }
        else if (key == "s") haveSuffix = from_hex(value, s.suffix) && !s.suffix.empty();
        else if (key == "z") { i64 z = 0; if (!parse_i64(value, z) || z > 18) return false; s.padding = static_cast<u32>(z); }
        else if (key == "a") haveFirst = parse_i64(value, s.first);
        else if (key == "b") haveLast = parse_i64(value, s.last);
        else if (key == "r") {
            char* end = nullptr;
            const f64 fps = std::strtod(value.c_str(), &end);
            if (!end || *end || !std::isfinite(fps) || fps <= 0.0 || fps > 1000.0) return false;
            s.fps = fps;
        } else if (key == "al") {
            s.alpha = value == "0" ? SequenceAlphaMode::Ignore : value == "2" ? SequenceAlphaMode::Premultiplied : SequenceAlphaMode::Straight;
        } else if (key == "c") {
            s.color = value == "1" ? SequenceColorMode::Linear : SequenceColorMode::Srgb;
        } else if (key == "n") {
            usize k = 0;
            while (k < value.size()) {
                usize c = value.find(',', k);
                if (c == std::string::npos) c = value.size();
                const std::string range = value.substr(k, c - k);
                k = c + 1;
                const usize dash = range.find('-');
                i64 a = 0, b = 0;
                if (dash == std::string::npos) {
                    if (!parse_i64(range, a)) return false;
                    b = a;
                } else if (!parse_i64(range.substr(0, dash), a) || !parse_i64(range.substr(dash + 1), b)) {
                    return false;
                }
                if (b < a || (!s.present.empty() && a <= s.present.back().second + 1)) return false;
                s.present.emplace_back(a, b);
                if (s.present.size() > kImageSequenceMaxFrames) return false;
            }
        }
    }
    if (!haveVersion || !haveDir || !haveSuffix || !haveFirst || !haveLast || s.last < s.first) return false;
    if (static_cast<u64>(s.last - s.first) + 1 > kImageSequenceMaxFrames) return false;
    if (!s.present.empty() && (s.present.front().first != s.first || s.present.back().second != s.last)) return false;
    if (s.present.size() == 1) s.present.clear();
    out = std::move(s);
    return true;
}

bool image_sequence_extension(const std::string& fileName) noexcept {
    try {
        const std::string ext = extension_of(fileName);
        return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "bmp" || ext == "exr";
    } catch (...) { return false; }
}

// =============================================================================
// Detecção
// =============================================================================

bool detect_image_sequence(const std::vector<std::string>& files, const std::string& preferred,
                           ImageSequenceSpec& out, u32* candidates) {
    struct Item { std::string full; std::string digits; i64 number = 0; };
    struct Group { std::string dir, prefix, suffix; std::vector<Item> items; bool preferred = false; };
    std::map<std::string, Group> groups;
    std::string preferredKey;
    const std::string preferredNorm = preferred.empty() ? std::string{} : utf8_of(fs::u8path(preferred).lexically_normal());
    for (const std::string& file : files) {
        const fs::path p = fs::u8path(file).lexically_normal();
        const std::string name = utf8_of(p.filename());
        if (name.empty() || name[0] == '.' || !image_sequence_extension(name)) continue;
        const usize dot = name.find_last_of('.');
        // Último grupo de dígitos antes da extensão: "render_0001", "shot.0001",
        // "frame_0001_beauty".
        usize end = dot;
        while (end > 0 && !std::isdigit(static_cast<unsigned char>(name[end - 1]))) --end;
        if (end == 0) continue;
        usize start = end;
        while (start > 0 && std::isdigit(static_cast<unsigned char>(name[start - 1]))) --start;
        const std::string digits = name.substr(start, end - start);
        if (digits.empty() || digits.size() > 12) continue;
        Item item;
        item.full = utf8_of(p);
        item.digits = digits;
        if (!parse_i64(digits, item.number)) continue;
        const std::string dir = utf8_of(p.parent_path());
        const std::string prefix = name.substr(0, start), suffix = name.substr(end);
        const std::string key = dir + '\x01' + prefix + '\x01' + suffix;
        Group& g = groups[key];
        if (g.items.empty()) { g.dir = dir; g.prefix = prefix; g.suffix = suffix; }
        if (!preferredNorm.empty() && item.full == preferredNorm) { g.preferred = true; preferredKey = key; }
        g.items.push_back(std::move(item));
    }
    if (candidates) *candidates = static_cast<u32>(groups.size());
    if (groups.empty()) return false;
    // O quadro escolhido não tem número (ou não é imagem): não é uma sequência —
    // nada de importar outra que por acaso esteja na mesma pasta.
    if (!preferredNorm.empty() && preferredKey.empty()) return false;
    const Group* best = nullptr;
    if (!preferredKey.empty()) best = &groups[preferredKey];
    else for (const auto& [key, g] : groups) if (!best || g.items.size() > best->items.size()) best = &g;
    // Dígitos: o menor comprimento entre os que começam com zero ("0001");
    // sem zeros à esquerda, o comprimento comum, ou largura livre.
    usize padding = 0;
    bool anyLeadingZero = false, sameLength = true;
    for (const Item& it : best->items) {
        if (it.digits.size() > 1 && it.digits[0] == '0') {
            padding = anyLeadingZero ? std::min(padding, it.digits.size()) : it.digits.size();
            anyLeadingZero = true;
        }
        sameLength = sameLength && it.digits.size() == best->items.front().digits.size();
    }
    if (!anyLeadingZero) padding = sameLength ? best->items.front().digits.size() : 0;
    ImageSequenceSpec s;
    s.directory = best->dir;
    s.prefix = best->prefix;
    s.suffix = best->suffix;
    s.padding = static_cast<u32>(padding);
    std::vector<i64> numbers;
    for (const Item& it : best->items) {
        std::string expect = std::to_string(it.number);
        if (expect.size() < padding) expect.insert(0, padding - expect.size(), '0');
        if (expect == it.digits) numbers.push_back(it.number);   // "01" e "001" não entram juntos
    }
    std::sort(numbers.begin(), numbers.end());
    numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
    if (numbers.empty()) return false;
    // Linha do tempo limitada: o que passa de kImageSequenceMaxFrames depois do
    // primeiro número fica de fora.
    while (static_cast<u64>(numbers.back() - numbers.front()) + 1 > kImageSequenceMaxFrames) numbers.pop_back();
    s.first = numbers.front();
    s.last = numbers.back();
    for (i64 n : numbers) {
        if (!s.present.empty() && n == s.present.back().second + 1) s.present.back().second = n;
        else s.present.emplace_back(n, n);
    }
    if (s.present.size() == 1) s.present.clear();
    out = std::move(s);
    return true;
}

// =============================================================================
// Quadros
// =============================================================================

Status read_sequence_frame_header(const std::string& path, SequenceFrameHeader& out) noexcept {
    try {
        out = SequenceFrameHeader{};
        std::vector<u8> bytes;
        bool whole = false;
        if (!read_prefix(path, bytes, kHeaderProbeBytes, &whole)) return Status{Errc::MediaSourceMissing, "quadro ausente"};
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (exr_magic(bytes.data(), bytes.size())) {
                ExrHeaderGuard g;
                if (parse_exr_header(bytes.data(), bytes.size(), g).ok()) {
                    i64 x0 = 0, y0 = 0, w = 0, h = 0;
                    exr_frame_box(g.header, x0, y0, w, h);
                    if (w <= 0 || h <= 0) return Status{Errc::CorruptData, "EXR sem pixels"};
                    out.width = static_cast<u32>(w);
                    out.height = static_cast<u32>(h);
                    out.channels = static_cast<u32>(g.header.num_channels);
                    out.hasAlpha = exr_channel(g.header, 'A') >= 0;
                    out.bitDepth = 16;
                    for (int c = 0; c < g.header.num_channels; ++c)
                        if (g.header.pixel_types[c] != TINYEXR_PIXELTYPE_HALF) out.bitDepth = 32;
                    out.exr = true;
                    out.fps = exr_fps(g.header);
                    return OkStatus;
                }
            } else if (bytes.size() <= static_cast<usize>(std::numeric_limits<int>::max())) {
                int w = 0, h = 0, c = 0;
                const int n = static_cast<int>(bytes.size());
                if (stbi_info_from_memory(bytes.data(), n, &w, &h, &c) && w > 0 && h > 0) {
                    out.width = static_cast<u32>(w);
                    out.height = static_cast<u32>(h);
                    out.channels = static_cast<u32>(c);
                    out.hasAlpha = c == 2 || c == 4;
                    out.bitDepth = stbi_is_16_bit_from_memory(bytes.data(), n) ? 16 : 8;
                    return OkStatus;
                }
            }
            // Cabeçalho além do começo lido (JPEG com EXIF grande): o arquivo inteiro.
            if (whole || !fileio::read_all(path, bytes, kMaxFileBytes)) break;
            whole = true;
        }
        return Status{Errc::UnsupportedFormat, "quadro ilegivel"};
    } catch (const std::bad_alloc&) {
        return Status{Errc::OutOfMemory};
    } catch (...) {
        return Status{Errc::IoError};
    }
}

Status decode_sequence_frame(const std::string& path, SequencePixels& out, bool premultiplied) noexcept {
    try {
        out = SequencePixels{};
        std::vector<u8> bytes;
        if (!fileio::read_all(path, bytes, kMaxFileBytes) || bytes.empty()) {
            return fileio::exists(path) ? Status{Errc::BudgetExceeded, "quadro grande demais"}
                                        : Status{Errc::MediaSourceMissing, "quadro ausente"};
        }
        if (exr_magic(bytes.data(), bytes.size())) return decode_exr(bytes.data(), bytes.size(), out);
        return decode_stb(bytes.data(), bytes.size(), premultiplied, out);
    } catch (const std::bad_alloc&) {
        out = SequencePixels{};
        return Status{Errc::OutOfMemory, "sem memoria para o quadro"};
    } catch (...) {
        out = SequencePixels{};
        return Status{Errc::DecodeFailed};
    }
}

u64 image_sequence_ahead_decodes() noexcept { return gAheadDecodes.load(std::memory_order_relaxed); }

std::unique_ptr<VideoDecoderBackend> open_image_sequence(const ImageSequenceSpec& spec, const std::string& directory,
        MediaPriority priority, MemoryManager* memory, u32 width, u32 height) {
    try {
        if (directory.empty() || spec.frame_count() <= 0) return nullptr;
        if (!width || !height) {
            SequenceFrameHeader header;
            const std::string first = utf8_of(fs::u8path(directory) / fs::u8path(spec.file_name(spec.first)));
            if (!read_sequence_frame_header(first, header).ok()) return nullptr;
            width = header.width;
            height = header.height;
        }
        auto backend = std::make_unique<ImageSequenceBackend>(spec, directory, priority, memory, width, height);
        if (!backend->valid()) return nullptr;
        return backend;
    } catch (...) {
        return nullptr;
    }
}

// =============================================================================
// Fábrica
// =============================================================================

std::string ImageSequenceFactory::resolve_directory(const std::string& stored) const {
    if (stored.empty()) return {};
    return resolve_ ? resolve_(stored) : stored;
}

bool ImageSequenceFactory::probe(const char* sourcePath, MediaProbe& out) {
    if (!sourcePath || !is_image_sequence(sourcePath)) return inner_ && inner_->probe(sourcePath, out);
    try {
        ImageSequenceSpec spec;
        if (!decode_image_sequence(sourcePath, spec)) return false;
        const std::string dir = resolve_directory(spec.directory);
        if (dir.empty()) return false;
        SequenceFrameHeader header;
        const std::string first = utf8_of(fs::u8path(dir) / fs::u8path(spec.file_name(spec.first)));
        if (!read_sequence_frame_header(first, header).ok()) return false;
        out = MediaProbe{};
        out.hasVideo = true;
        out.video.codedWidth = header.width;
        out.video.codedHeight = header.height;
        out.video.fps = spec.fps;
        out.video.durationUs = static_cast<i64>(std::llround(static_cast<f64>(spec.frame_count()) * 1e6 / spec.fps));
        out.video.color.fullRange = true;
        out.video.color.bitDepth = static_cast<u8>(std::min<u32>(header.bitDepth, 16));
        out.video.color.transfer = spec.color == SequenceColorMode::Linear ? TransferFunction::Linear : TransferFunction::SRGB;
        out.video.color.fromStream = true;
        out.video.preciseFrameTiming = true;
        const std::string ext = extension_of(spec.suffix);
        std::snprintf(out.video.codec, sizeof(out.video.codec), "image/%s", ext.c_str());
        std::snprintf(out.video.decoderName, sizeof(out.video.decoderName), "aurea-seq-%s", ext.c_str());
        std::error_code ec;
        for (i64 n : spec.present_numbers()) {
            const auto size = fs::file_size(fs::u8path(dir) / fs::u8path(spec.file_name(n)), ec);
            if (!ec) out.fileBytes += size;
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string ImageSequenceFactory::cache_identity(const char* sourcePath) {
    if (!sourcePath || !is_image_sequence(sourcePath)) return inner_ ? inner_->cache_identity(sourcePath) : VideoSourceFactory::cache_identity(sourcePath);
    try {
        ImageSequenceSpec spec;
        if (!decode_image_sequence(sourcePath, spec)) return {};
        const std::string dir = resolve_directory(spec.directory);
        if (dir.empty()) return {};
        // A receita e a versão do primeiro e do último arquivo.
        u64 h = 1469598103934665603ull;
        for (unsigned char c : std::string(sourcePath)) { h ^= c; h *= 1099511628211ull; }
        std::string id = "seq:" + std::to_string(h);
        for (i64 n : {spec.first, spec.last}) {
            const std::string v = VideoSourceFactory::cache_identity(utf8_of(fs::u8path(dir) / fs::u8path(spec.file_name(n))).c_str());
            if (v.empty()) return {};
            id += ":" + v;
        }
        return id;
    } catch (...) {
        return {};
    }
}

std::unique_ptr<VideoDecoderBackend> ImageSequenceFactory::open_video(const Asset& asset, MediaPriority priority) {
    if (!is_image_sequence(asset.sourcePath)) return inner_ ? inner_->open_video(asset, priority) : nullptr;
    try {
        ImageSequenceSpec spec;
        if (!decode_image_sequence(asset.sourcePath, spec)) return nullptr;
        const std::string dir = resolve_directory(spec.directory);
        if (dir.empty()) {
            AUREA_LOG_WARN("sequencia: pasta nao resolvida");
            return nullptr;
        }
        return open_image_sequence(spec, dir, priority, memory_, asset.video.width, asset.video.height);
    } catch (...) {
        return nullptr;
    }
}

std::unique_ptr<audio::AudioDecoderBackend> ImageSequenceFactory::open_audio(const char* sourcePath) {
    if (sourcePath && is_image_sequence(sourcePath)) return nullptr;
    return inner_ ? inner_->open_audio(sourcePath) : nullptr;
}

std::unique_ptr<audio::AudioDecoderBackend> ImageSequenceFactory::open_audio_software(const char* sourcePath) {
    if (sourcePath && is_image_sequence(sourcePath)) return nullptr;
    return inner_ ? inner_->open_audio_software(sourcePath) : nullptr;
}

} // namespace aurea
