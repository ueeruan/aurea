#include "aurea/project/AeProject.hpp"

#include "aurea/project/Presets.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_set>

namespace aurea::ae {
namespace {

constexpr usize kMaxFile = 64u << 20;
constexpr usize kMaxLayers = 1024;
constexpr usize kMaxComps = 128;
constexpr usize kMaxMedia = 1024;
constexpr usize kMaxFolderDepth = 32;
constexpr usize kMaxChunks = 50000;
constexpr usize kMaxPropertyBytes = 12u << 20;

u8 byte(std::string_view bytes, usize off) {
    return static_cast<u8>(bytes[off]);
}

u32 word(std::string_view bytes, usize off) {
    return (static_cast<u32>(byte(bytes, off)) << 8) | byte(bytes, off + 1);
}

u32 integer(std::string_view bytes, usize off) {
    return (word(bytes, off) << 16) | word(bytes, off + 2);
}

i32 signed_integer(std::string_view bytes, usize off) {
    return std::bit_cast<i32>(integer(bytes, off));
}

f64 time_pair(std::string_view bytes, usize off, bool signedValue = true) {
    const u32 divisor = integer(bytes, off + 4);
    const f64 dividend = signedValue ? static_cast<f64>(signed_integer(bytes, off)) : integer(bytes, off);
    return divisor ? dividend / divisor : 0.0;
}

std::string display_name(std::string_view text) {
    text = text.substr(0, std::min<usize>(text.find('\0'), 1024));
    std::string result;
    for (usize i = 0; i < text.size();) {
        const u8 c = byte(text, i);
        const usize n = c < 0x80 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 :
                        c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        bool good = n && n <= text.size() - i;
        for (usize j = 1; good && j < n; ++j) good = (byte(text, i + j) & 0xc0) == 0x80;
        if (good && n > 2) {
            const u8 next = byte(text, i + 1);
            good = (c != 0xe0 || next >= 0xa0) && (c != 0xed || next < 0xa0) &&
                   (c != 0xf0 || next >= 0x90) && (c != 0xf4 || next < 0x90);
        }
        if (good) { result.append(text.substr(i, n)); i += n; }
        else { result.push_back('?'); ++i; }
    }
    return result;
}

struct Chunk {
    std::string_view tag, bytes;
    [[nodiscard]] bool list(std::string_view type) const {
        return tag == "LIST" && bytes.substr(0, 4) == type;
    }
};

struct Reader {
    ProjectDocument& out;
    std::string& error;
    std::unordered_set<u32> itemIds;
    usize chunks = 0, layers = 0, propertyBytes = 0, expandedBytes = 0;

    bool fail(const char* message) {
        if (error.empty()) error = message;
        return false;
    }

    template<class Fn>
    bool each(std::string_view bytes, Fn visit) {
        while (!bytes.empty()) {
            if (++chunks > kMaxChunks) return fail("muitos blocos no projeto");
            if (bytes.size() < 8) return fail("cabeçalho de bloco incompleto");
            const usize size = integer(bytes, 4);
            const usize available = bytes.size() - 8;
            if (size > available || (size & 1u) > available - size)
                return fail("bloco fora dos limites do arquivo");
            Chunk c{bytes.substr(0, 4), bytes.substr(8, size)};
            if (c.tag == "LIST" && size < 4) return fail("grupo sem identificador");
            if (!visit(c)) return false;
            bytes.remove_prefix(8 + size + (size & 1u));
        }
        return true;
    }

    bool media(ProjectMedia& m, std::string_view payload) {
        std::string_view info, options, path;
        if (!each(payload, [&](const Chunk& c) {
            if (c.tag == "sspc") info = c.bytes;
            if (c.tag == "opti") options = c.bytes;
            if (c.list("Als2")) return each(c.bytes.substr(4), [&](const Chunk& entry) {
                if (entry.tag == "alas") path = entry.bytes;
                return true;
            });
            return true;
        })) return false;
        if (info.size() < 0x66) return fail("descrição de mídia incompleta");
        m.width = std::min(integer(info, 0x1e), 16384u);
        m.height = std::min(integer(info, 0x22), 16384u);
        m.duration = time_pair(info, 0x26, false);
        m.fps = static_cast<f64>(integer(info, 0x3a)) / 65536.0;
        m.channels = std::min(word(info, 0x64), 32u);
        m.solid = info.substr(0x16, 4) == "Soli";
        if (m.solid && options.substr(0, 4) == "Soli" && options.size() >= 0x1a) {
            for (usize i = 0; i < 3; ++i) {
                const f32 v = std::bit_cast<f32>(integer(options, 0x0e + i * 4));
                if (std::isfinite(v)) m.color[i] = std::clamp(v, 0.0f, 1.0f);
            }
            if (m.name.empty()) m.name = display_name(options.substr(0x1a));
        }
        // Use the source path as a matching hint inside the package, never as a local path.
        if (!m.solid && !path.empty() && path.size() <= 8192) {
            json::Value alias;
            if (json::parse(path, alias)) {
                if (const auto* full = alias.get("fullpath"); full && full->is_string()) {
                    const auto name = std::string_view(full->string);
                    m.sourceReference = display_name(name);
                    const usize sep = name.find_last_of("/\\");
                    m.filename = display_name(sep == std::string_view::npos ? name : name.substr(sep + 1));
                }
            }
        }
        if (m.name.empty()) m.name = m.filename.empty() ? (m.solid ? "Sólido" : "Mídia do AE") : m.filename;
        return true;
    }

    bool layer(ProjectLayer& l, std::string_view payload, u32 timescale) {
        std::string_view data, props;
        bool named = false;
        if (++layers > kMaxLayers) return fail("muitas camadas no projeto");
        if (!each(payload, [&](const Chunk& c) {
            if (c.tag == "ldta") data = c.bytes;
            if (c.tag == "Utf8" && !named) { named = true; l.name = display_name(c.bytes); }
            if (c.list("tdgp") && props.empty()) props = c.bytes;
            return true;
        })) return false;
        if (data.size() < 160) return fail("dados de camada incompletos");
        l.id = integer(data, 0);
        l.source = integer(data, 0x28);
        l.parent = integer(data, 0x84);
        l.legacyMatte = data.size() < 164;
        l.matte = l.legacyMatte ? 0 : integer(data, 0xa0);
        l.type = byte(data, 0x83); l.light = byte(data, 0x8b);
        l.flagsA = byte(data, 0x25); l.flagsB = byte(data, 0x26); l.flagsC = byte(data, 0x27);
        l.blend = byte(data, 0x63); l.transfer = byte(data, 0x67); l.matteMode = byte(data, 0x6b);
        const u32 divisor = integer(data, 0x6c);
        if (!divisor || !integer(data, 0x10) || !integer(data, 0x18) || !integer(data, 0x20))
            return fail("tempo de camada inválido");
        l.stretch = static_cast<f64>(signed_integer(data, 8)) / divisor;
        l.start = time_pair(data, 0x0c);
        l.in = time_pair(data, 0x14);
        l.out = time_pair(data, 0x1c);
        if (l.id == 0 || l.stretch == 0 || std::abs(l.stretch) > 1000.0 ||
            std::abs(l.start) > 7 * 86400 || std::abs(l.in) > 7 * 86400 || std::abs(l.out) > 7 * 86400)
            return fail("tempo ou identificador de camada fora dos limites");
        if (props.size() > kMaxPropertyBytes - propertyBytes)
            return fail("propriedades demais no projeto");
        propertyBytes += props.size();
        return props.empty() || parse_property_tree(props, timescale, expandedBytes, l.properties, error);
    }

    bool item(std::string_view payload, usize depth) {
        std::string_view itemData, compData, pin, contents;
        std::vector<std::string_view> layerData;
        std::string name;
        bool named = false;
        if (!each(payload, [&](const Chunk& c) {
            if (c.tag == "idta") itemData = c.bytes;
            if (c.tag == "cdta") compData = c.bytes;
            if (c.tag == "Utf8" && !named) { named = true; name = display_name(c.bytes); }
            if (c.list("Pin ") && pin.empty()) pin = c.bytes.substr(4);
            if (c.list("Sfdr")) contents = c.bytes.substr(4);
            if (c.list("Layr")) {
                if (layerData.size() >= kMaxLayers) return fail("muitas camadas na composição");
                layerData.push_back(c.bytes.substr(4));
            }
            return true;
        })) return false;
        if (itemData.size() < 20) return fail("identificador de item incompleto");
        const u32 id = integer(itemData, 16);
        const u32 type = word(itemData, 0);
        if (type == 1) return folder(contents, depth + 1);
        if (type != 4 && type != 7) return true;
        if (!id || !itemIds.insert(id).second) return fail("identificador de item repetido");
        if (type == 7) {
            if (pin.empty()) return true;
            if (out.media.size() >= kMaxMedia) return fail("muitos itens de mídia");
            auto& m = out.media.emplace_back();
            m.id = id; m.name = std::move(name);
            return media(m, pin);
        }
        if (out.comps.size() >= kMaxComps) return fail("muitas composições");
        if (compData.size() < 0xa0) return fail("dados de composição incompletos");
        auto& comp = out.comps.emplace_back();
        comp.id = id; comp.name = name.empty() ? "Composição do AE" : std::move(name);
        comp.width = word(compData, 0x8c); comp.height = word(compData, 0x8e);
        const u32 timescale = integer(compData, 8);
        const u32 timePerFrame = integer(compData, 4);
        comp.fps = timePerFrame ? static_cast<f64>(timescale) / timePerFrame
                               : static_cast<f64>(integer(compData, 0x9c)) / 65536.0;
        comp.duration = time_pair(compData, 0x2c, false);
        comp.motionBlur = (integer(compData, 0x88) & 8) != 0;
        if (compData.size() >= 0xb8) {
            comp.shutterAngle = std::clamp(signed_integer(compData, 0xac), 0, 720);
            comp.shutterPhase = std::clamp(signed_integer(compData, 0xb4), -360, 360);
        }
        if (!comp.width || !comp.height || comp.width > 8192 || comp.height > 8192 ||
            static_cast<u64>(comp.width) * comp.height > 32u * 1024 * 1024 ||
            comp.fps < 1 || comp.fps > 1000 || comp.duration <= 0 || comp.duration > 86400)
            return fail("resolução, FPS ou duração fora dos limites");
        std::unordered_set<u32> ids;
        for (const auto bytes : layerData) {
            auto& l = comp.layers.emplace_back();
            if (!layer(l, bytes, timescale)) return false;
            if (!ids.insert(l.id).second) return fail("identificador de camada repetido");
        }
        return true;
    }

    bool folder(std::string_view payload, usize depth) {
        if (depth > kMaxFolderDepth) return fail("pastas aninhadas demais");
        return each(payload, [&](const Chunk& c) {
            return !c.list("Item") || item(c.bytes.substr(4), depth);
        });
    }
};
} // namespace

bool is_project_file(std::string_view bytes) noexcept {
    return bytes.size() >= 12 && bytes.substr(0, 4) == "RIFX" && bytes.substr(8, 4) == "Egg!";
}

bool parse_project(std::string_view bytes, ProjectDocument& out, std::string& error) {
    out = {};
    error.clear();
    if (!is_project_file(bytes)) { error = "assinatura RIFX/Egg! ausente"; return false; }
    if (bytes.size() > kMaxFile) { error = "arquivo grande demais"; return false; }
    const usize size = integer(bytes, 4);
    if (size < 4 || size > bytes.size() - 8) { error = "projeto truncado"; return false; }
    ProjectDocument parsed;
    Reader reader{parsed, error};
    bool sawFolder = false;
    if (!reader.each(bytes.substr(12, size - 4), [&](const Chunk& c) {
        if (!c.list("Fold")) return true;
        if (sawFolder) return reader.fail("mais de uma pasta raiz");
        sawFolder = true;
        return reader.folder(c.bytes.substr(4), 0);
    })) return false;
    if (parsed.comps.empty()) { error = "nenhuma composição encontrada"; return false; }
    out = std::move(parsed);
    return true;
}

} // namespace aurea::ae
