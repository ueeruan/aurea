#include "aurea/project/AeProject.hpp"

#include "aurea/project/Presets.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_set>

namespace aurea::ae {
namespace {

// 1.0.8: os tetos valem por item. Composição, camada ou mídia malformada ou
// além do limite fica de fora (ProjectDocument::issues) e o resto entra.
constexpr usize kMaxFile = 256u << 20;
constexpr usize kMaxLayers = 8192;        // por composição
constexpr usize kMaxComps = 2048;
constexpr usize kMaxMedia = 16384;
constexpr usize kMaxFolderDepth = 64;
constexpr usize kMaxChunks = 1000000;
constexpr usize kMaxPropertyBytes = kMaxFile;
// Maior lado e área de uma composição no Aurea (textura máxima do render).
constexpr u32 kMaxCompSide = 8192;
constexpr u64 kMaxCompArea = 32ull * 1024 * 1024;

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
    usize chunks = 0, propertyBytes = 0, expandedBytes = 0;
    bool stopped = false;   ///< teto global (blocos): para de ler e fica o que entrou
    std::string itemName;   ///< nome do item em leitura (para o relatório)

    bool fail(const char* message) {
        if (error.empty()) error = message;
        return false;
    }

    void issue(std::string kind, std::string name, std::string reason, bool skipped = true) {
        if (out.issues.size() < 400) out.issues.push_back({std::move(kind), std::move(name), std::move(reason), skipped});
    }

    template<class Fn>
    bool each(std::string_view bytes, Fn visit) {
        while (!bytes.empty()) {
            if (++chunks > kMaxChunks) {
                stopped = true;
                return fail("muitos blocos no projeto");
            }
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
        if (props.empty()) return true;
        // Propriedades ilegíveis: a camada entra sem elas (tempo e fonte valem).
        std::string why;
        if (parse_property_tree(props, timescale, expandedBytes, l.properties, why)) return true;
        l.properties = {};
        issue("camada", l.name.empty() ? "Camada " + std::to_string(l.id) : l.name,
              "propriedades ilegíveis (" + why + "); a camada entrou com os valores padrão");
        return true;
    }

    /// Lado e área máximos do Aurea: a composição maior é reduzida na mesma
    /// proporção (o conteúdo acompanha na importação).
    void fit(ProjectComp& comp) {
        f64 k = std::min({1.0, static_cast<f64>(kMaxCompSide) / comp.width, static_cast<f64>(kMaxCompSide) / comp.height});
        const f64 area = static_cast<f64>(comp.width) * comp.height * k * k;
        if (area > static_cast<f64>(kMaxCompArea)) k *= std::sqrt(static_cast<f64>(kMaxCompArea) / area);
        comp.fitWidth = std::clamp<u32>(static_cast<u32>(std::floor(comp.width * k + 1e-6)), 1, kMaxCompSide);
        comp.fitHeight = std::clamp<u32>(static_cast<u32>(std::floor(comp.height * k + 1e-6)), 1, kMaxCompSide);
        if (k < 1.0)
            issue("composicao", comp.name, std::to_string(comp.width) + "x" + std::to_string(comp.height) +
                  " px foi reduzida para " + std::to_string(comp.fitWidth) + "x" + std::to_string(comp.fitHeight) +
                  " px (limite do Aurea); o conteúdo acompanha", false);
    }

    bool item(std::string_view payload, usize depth) {
        std::string_view itemData, compData, pin, contents;
        std::vector<std::string_view> layerData;
        std::string name;
        bool named = false;
        usize extraLayers = 0;
        itemName.clear();
        if (!each(payload, [&](const Chunk& c) {
            if (c.tag == "idta") itemData = c.bytes;
            if (c.tag == "cdta") compData = c.bytes;
            if (c.tag == "Utf8" && !named) { named = true; name = display_name(c.bytes); itemName = name; }
            if (c.list("Pin ") && pin.empty()) pin = c.bytes.substr(4);
            if (c.list("Sfdr")) contents = c.bytes.substr(4);
            if (c.list("Layr")) {
                if (layerData.size() >= kMaxLayers) ++extraLayers;
                else layerData.push_back(c.bytes.substr(4));
            }
            return true;
        })) return false;
        if (itemData.size() < 20) return fail("identificador de item incompleto");
        const u32 id = integer(itemData, 16);
        const u32 type = word(itemData, 0);
        if (type == 1) {
            if (depth + 1 > kMaxFolderDepth) {
                issue("pasta", name, "pastas aninhadas demais; o conteúdo desta pasta não foi lido");
                return true;
            }
            if (folder(contents, depth + 1)) return true;
            if (stopped) return false;
            // O que a pasta tinha antes do trecho corrompido fica.
            issue("pasta", name, "parte da pasta está corrompida (" + error + ")");
            error.clear();
            return true;
        }
        if (type != 4 && type != 7) return true;
        if (!id || !itemIds.insert(id).second) return fail("identificador de item repetido");
        if (type == 7) {
            if (pin.empty()) return true;
            if (out.media.size() >= kMaxMedia) {
                issue("midia", name, "o projeto passa de " + std::to_string(kMaxMedia) + " mídias; esta ficou de fora");
                return true;
            }
            auto& m = out.media.emplace_back();
            m.id = id; m.name = std::move(name);
            return media(m, pin);
        }
        if (out.comps.size() >= kMaxComps) {
            issue("composicao", name, "o projeto passa de " + std::to_string(kMaxComps) + " composições; esta ficou de fora");
            return true;
        }
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
        if (!comp.width || !comp.height) return fail("composição sem resolução");
        // FPS ou duração fora da faixa: o valor aceito mais próximo, com nota.
        if (!(comp.fps >= 1 && comp.fps <= 1000)) {
            issue("composicao", comp.name, "taxa de quadros inválida no arquivo; ficou 30 fps", false);
            comp.fps = 30;
        }
        if (!(comp.duration > 0 && comp.duration <= 86400)) {
            issue("composicao", comp.name, "duração inválida no arquivo; ficou a mais próxima aceita", false);
            comp.duration = comp.duration > 86400 ? 86400 : 10;
        }
        fit(comp);
        if (extraLayers)
            issue("composicao", comp.name, std::to_string(extraLayers) + " camadas além do limite de " +
                  std::to_string(kMaxLayers) + " por composição ficaram de fora");
        std::unordered_set<u32> ids;
        for (const auto bytes : layerData) {
            auto& l = comp.layers.emplace_back();
            if (!layer(l, bytes, timescale)) {
                if (stopped) return false;
                // Camada ilegível: fica de fora; a composição segue com as outras.
                const std::string shown = l.name.empty() ? "camada sem nome" : l.name;
                issue("camada", comp.name + " > " + shown, error);
                error.clear();
                comp.layers.pop_back();
                continue;
            }
            if (!ids.insert(l.id).second) {
                issue("camada", comp.name + " > " + l.name, "identificador de camada repetido; a cópia ficou de fora");
                comp.layers.pop_back();
            }
        }
        return true;
    }

    bool folder(std::string_view payload, usize depth) {
        return each(payload, [&](const Chunk& c) {
            if (!c.list("Item")) return true;
            const usize comps = out.comps.size(), media = out.media.size();
            if (item(c.bytes.substr(4), depth)) return true;
            if (stopped) return false;
            // Item malformado: fica de fora (com o motivo) e a leitura segue.
            out.comps.resize(comps);
            out.media.resize(media);
            issue("item", itemName.empty() ? "item sem nome" : itemName, error);
            error.clear();
            return true;
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
    const bool read = reader.each(bytes.substr(12, size - 4), [&](const Chunk& c) {
        if (!c.list("Fold")) return true;
        // Uma segunda pasta raiz (arquivo emendado) não é lida.
        if (sawFolder) {
            reader.issue("pasta", "pasta raiz", "mais de uma pasta raiz no arquivo; só a primeira foi lida");
            return true;
        }
        sawFolder = true;
        return reader.folder(c.bytes.substr(4), 0);
    });
    // O teto de blocos para a leitura, mas o que já entrou fica.
    if (!read && !(reader.stopped && !parsed.comps.empty())) return false;
    if (!read) {
        reader.issue("projeto", "projeto", "o arquivo passa de " + std::to_string(kMaxChunks) +
                     " blocos; o restante não foi lido");
        error.clear();
    }
    if (parsed.comps.empty()) { error = "nenhuma composição encontrada"; return false; }
    out = std::move(parsed);
    return true;
}

} // namespace aurea::ae
