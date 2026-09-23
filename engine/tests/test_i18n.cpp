// =============================================================================
//  Idiomas (Fase 8.1): o que o motor precisa garantir para o app falar sete
//  idiomas — identidade estável no catálogo de efeitos (a UI traduz pelo ID,
//  nunca pelo texto), shaping de escritas complexas e nomes/caminhos Unicode.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/effects/EffectRegistry.hpp"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

using namespace aurea;

namespace {

std::string json_str(const char* s) {
    std::string out = "\"";
    for (const char* p = s ? s : ""; *p; ++p) {
        const char c = *p;
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out + "\"";
}

} // namespace

// A UI traduz nome de efeito pela CHAVE ("aurea.blur.gaussian") e rótulo de
// parâmetro pelo ID ("blurriness"). Um ID vazio ou repetido dentro do efeito
// faria duas linhas do painel mostrarem a mesma tradução — ou nenhuma.
//
// Com `AUREA_CATALOG_JSON=<arquivo>` o teste também grava o catálogo inteiro
// (chave, nome, categoria, parâmetros com id/rótulo/opções): é a entrada de
// `tools/i18n_effects.py`, que gera a tabela Kotlin chave → recurso.
AUREA_TEST(I18n, EffectCatalogHasStableIdsForEveryLabel) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    std::set<std::string> keys;
    u32 params = 0, enums = 0, bad = 0;
    std::string json = "[\n";
    for (u32 i = 0; i < reg.count(); ++i) {
        const EffectInfo& info = reg.at(i).info();
        AUREA_CHECK_MSG(keys.insert(info.key).second, info.key);
        AUREA_CHECK(info.name && *info.name);
        const ParameterRegistry& p = reg.params_at(i);
        std::set<std::string> ids;
        json += "  {\"key\": " + json_str(info.key) + ", \"name\": " + json_str(info.name) +
                ", \"category\": " + json_str(info.category) + ", \"params\": [";
        for (u32 j = 0; j < p.count(); ++j) {
            const ParamSpec& s = p.at(j);
            const bool okId = s.id && *s.id && ids.insert(s.id).second;
            if (!okId) { ++bad; std::printf("    id ruim: %s #%u\n", info.key, j); }
            ++params;
            json += std::string(j ? ", " : "") + "{\"index\": " + std::to_string(j) + ", \"id\": " + json_str(s.id) +
                    ", \"label\": " + json_str(s.label) + ", \"hidden\": " +
                    ((s.flags & kParamHidden) ? "true" : "false") + ", \"enum\": [";
            for (u32 k = 0; k < s.enumCount && s.enumLabels; ++k) {
                json += std::string(k ? ", " : "") + json_str(s.enumLabels[k]);
                ++enums;
            }
            json += "]}";
        }
        json += std::string("]}") + (i + 1 < reg.count() ? "," : "") + "\n";
    }
    json += "]\n";
    std::printf("    catalogo: %u efeitos, %u parametros, %u opcoes; ids ruins %u\n", reg.count(), params, enums, bad);
    AUREA_CHECK(reg.count() > 0);
    AUREA_CHECK(bad == 0);
    if (const char* path = std::getenv("AUREA_CATALOG_JSON")) {
        if (FILE* f = std::fopen(path, "wb")) {
            std::fwrite(json.data(), 1, json.size(), f);
            std::fclose(f);
        }
    }
}

// -----------------------------------------------------------------------------
// Escritas complexas (HarfBuzz), cirílico na GPU e nomes/caminhos Unicode
// -----------------------------------------------------------------------------
#include "aurea/Engine.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

namespace {

/// Fonte do host; nula (com aviso honesto) se o arquivo não existir aqui.
std::shared_ptr<const text::Font> host_font(const char* path) {
    auto f = text::Font::load(path);
    if (!f) std::printf("    (fonte %s ausente no host: trecho pulado)\n", path);
    return f;
}

std::vector<text::ShapedGlyph> shape_utf8(const text::Font& f, const std::string& s) {
    TextData t;
    t.content = s;
    t.size = 100.0f;
    std::vector<text::ShapedGlyph> g;
    text::shaped_glyphs(f, t, g);
    return g;
}

/// Codepoints de um texto UTF-8 (para comparar com o nº de glifos).
std::vector<u32> codepoints(const std::string& s) {
    std::vector<u32> out;
    for (usize i = 0; i < s.size();) {
        const u8 c = static_cast<u8>(s[i]);
        const usize n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        u32 cp = n == 1 ? c : n == 2 ? (c & 0x1Fu) : n == 3 ? (c & 0x0Fu) : (c & 0x07u);
        for (usize k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<u8>(s[i + k]) & 0x3Fu);
        out.push_back(cp);
        i += n;
    }
    return out;
}

std::string utf8_of(u32 cp) {
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

/// Caminho a partir de UTF-8 (no Windows, `path(std::string)` leria ANSI).
std::filesystem::path u8_path(const std::string& s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string path_utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

} // namespace

// "مرحبا" (م ر ح ب ا): na palavra cada letra está numa forma de junção
// (inicial/final/medial) e tem de sair num glifo diferente da mesma letra
// sozinha (isolada); e a ordem na tela é da direita para a esquerda.
AUREA_TEST(I18n, ArabicMarhabaJoinsContextuallyRightToLeft) {
    auto f = host_font("C:/Windows/Fonts/arial.ttf");
    if (!f) return;
    const std::string word = "مرحبا";
    const std::vector<u32> cps = codepoints(word);
    const auto shaped = shape_utf8(*f, word);
    u32 differ = 0, notdef = 0, fallback = 0;
    std::string pairs;
    for (u32 i = 0; i < cps.size(); ++i) {
        const auto alone = shape_utf8(*f, utf8_of(cps[i]));
        const u32 iso = alone.empty() ? 0u : alone[0].glyph;
        u32 inWord = 0;
        for (const auto& g : shaped) if (g.cluster == i) inWord = g.glyph;
        differ += (iso != 0 && inWord != 0 && iso != inWord) ? 1u : 0u;
        pairs += std::to_string(iso) + "->" + std::to_string(inWord) + " ";
    }
    for (const auto& g : shaped) { notdef += g.glyph == 0 ? 1u : 0u; fallback += g.fallback ? 1u : 0u; }
    // RTL: o glifo mais à esquerda é o do ÚLTIMO caractere (ا); o mais à direita, o do primeiro (م).
    u32 leftmost = 99, rightmost = 99;
    f32 minX = 1e9f, maxX = -1e9f;
    for (const auto& g : shaped) {
        if (g.x < minX) { minX = g.x; leftmost = g.cluster; }
        if (g.x > maxX) { maxX = g.x; rightmost = g.cluster; }
    }
    std::printf("    marhaba: %zu codepoints, %zu glifos; isolado->na palavra: %s; %u de %zu diferem; "
                "esquerda = caractere %u, direita = %u; .notdef %u, reserva %u\n",
                cps.size(), shaped.size(), pairs.c_str(), differ, cps.size(), leftmost, rightmost, notdef, fallback);
    AUREA_CHECK(cps.size() == 5);
    // A Arial desenha o ر FINAL com o mesmo glifo do isolado (a letra só se
    // liga pela direita e a forma final dela é igual): 4 de 5 mudam de forma.
    AUREA_CHECK_MSG(differ >= 4, "letra de juncao com o glifo da forma isolada");
    AUREA_CHECK(leftmost == 4);
    AUREA_CHECK(rightmost == 0);
    AUREA_CHECK(notdef == 0);
    AUREA_CHECK(fallback == 0);
}

// "नमस्ते": o conjunto स्ते (स + ् + त + े) é UM cluster — o virama não vira
// glifo solto (o pontinho embaixo da letra), e nada de .notdef.
AUREA_TEST(I18n, DevanagariNamasteFormsConjunctCluster) {
    auto f = host_font("C:/Windows/Fonts/Nirmala.ttf");
    if (!f) return;
    const std::string word = "नमस्ते";
    const std::vector<u32> cps = codepoints(word);
    const auto shaped = shape_utf8(*f, word);
    // Glifo do virama sozinho (◌्): não pode aparecer na palavra.
    const auto viramaAlone = shape_utf8(*f, utf8_of(0x094D));
    u32 viramaGlyph = 0;
    for (const auto& g : viramaAlone) if (g.glyph != 0 && viramaGlyph == 0) viramaGlyph = g.glyph;
    u32 notdef = 0, fallback = 0, loneVirama = 0, inConjunct = 0;
    std::set<u32> clusters;
    std::string list;
    for (const auto& g : shaped) {
        notdef += g.glyph == 0 ? 1u : 0u;
        fallback += g.fallback ? 1u : 0u;
        loneVirama += g.glyph == viramaGlyph ? 1u : 0u;
        inConjunct += g.cluster == 2 ? 1u : 0u;
        clusters.insert(g.cluster);
        list += std::to_string(g.glyph) + "@" + std::to_string(g.cluster) + " ";
    }
    // Clusters: न (0), म (1) e o conjunto स्ते (2) — os 4 codepoints de स्ते
    // ficam juntos; nenhum glifo aponta para ्, त ou े sozinhos (3, 4, 5).
    std::printf("    namaste: %zu codepoints -> %zu glifos (glifo@cluster: %s); clusters distintos %zu; "
                "no conjunto %u glifo(s); virama solto %u (glifo %u); .notdef %u, reserva %u\n",
                cps.size(), shaped.size(), list.c_str(), clusters.size(), inConjunct, loneVirama, viramaGlyph, notdef,
                fallback);
    AUREA_CHECK(cps.size() == 6);
    AUREA_CHECK(shaped.size() < cps.size());
    AUREA_CHECK(clusters.size() == 3 && clusters.count(2) == 1 && *clusters.rbegin() == 2);
    AUREA_CHECK(inConjunct >= 1);
    AUREA_CHECK(viramaGlyph != 0 && loneVirama == 0);
    AUREA_CHECK(notdef == 0);
    AUREA_CHECK(fallback == 0);
}

// "Привет, мир": a Arial tem cirílico — um glifo por caractere, nenhum vazio
// (.notdef) nem de fonte de reserva.
AUREA_TEST(I18n, CyrillicShapesWithoutNotdefOrFallback) {
    auto f = host_font("C:/Windows/Fonts/arial.ttf");
    if (!f) return;
    const std::string s = "Привет, мир";
    const usize n = codepoints(s).size();
    const auto shaped = shape_utf8(*f, s);
    u32 notdef = 0, fallback = 0;
    for (const auto& g : shaped) { notdef += g.glyph == 0 ? 1u : 0u; fallback += g.fallback ? 1u : 0u; }
    std::printf("    cirilico: %zu codepoints -> %zu glifos; .notdef %u, reserva %u\n", n, shaped.size(), notdef, fallback);
    AUREA_CHECK(shaped.size() == n);
    AUREA_CHECK(notdef == 0);
    AUREA_CHECK(fallback == 0);
}

// Nome do projeto e caminho do arquivo em árabe, hindi, russo e com emoji
// (pasta também não-ASCII): salva, abre num motor NOVO e o título e o texto da
// camada voltam byte a byte.
AUREA_TEST(I18n, UnicodeProjectNamesAndPathsSaveAndReopen) {
    struct Case { const char* title; const char* file; };
    const Case cases[] = {
        {"مشروع تجريبي", "مشروع.aurea"},
        {"परियोजना", "परियोजना.aurea"},
        {"Проект тест", "проект.aurea"},
        {"Projeto 🎬✨", "🎬.aurea"},
    };
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / u8_path("aurea_i18n_ção");
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    AUREA_CHECK_MSG(!ec, "pasta temporaria nao criada");
    u32 same = 0;
    for (const Case& c : cases) {
        const std::filesystem::path file = dir / u8_path(c.file);
        const std::string path = path_utf8(file);
        bool saved = false;
        {
            Engine e;
            EngineConfig cfg;
            cfg.workerCount = 1;
            cfg.disableAutosave = true;
            AUREA_CHECK(e.initialize(cfg).ok());
            AUREA_CHECK(e.new_project(640, 360, 30.0, c.title).ok());
            AUREA_CHECK(e.add_text(c.title).ok());
            saved = e.save_project(path.c_str()).ok();
            e.shutdown();
        }
        const bool onDisk = std::filesystem::exists(file, ec);
        std::string title, content;
        bool loaded = false;
        {
            Engine e;
            EngineConfig cfg;
            cfg.workerCount = 1;
            cfg.disableAutosave = true;
            AUREA_CHECK(e.initialize(cfg).ok());
            loaded = e.load_project(path.c_str()).ok();
            if (loaded && e.project()) {
                title = e.project()->metadata().title;
                const Timeline& tl = e.project()->timeline();
                if (const Composition* comp = tl.composition(tl.current())) {
                    comp->layers().for_each([&](LayerId, const Layer& l) {
                        if (l.kind == LayerKind::Text) content = l.text.content;
                    });
                }
            }
            e.shutdown();
        }
        const bool ok = loaded && title == c.title && content == c.title;
        same += ok ? 1u : 0u;
        std::printf("    '%s' em .../%s: salvo %d, no disco %d, reaberto %d, titulo %zu/%zu bytes, texto %zu bytes, igual %d\n",
                    c.title, c.file, saved ? 1 : 0, onDisk ? 1 : 0, loaded ? 1 : 0, title.size(), std::strlen(c.title),
                    content.size(), ok ? 1 : 0);
        AUREA_CHECK_MSG(saved, c.file);
        AUREA_CHECK_MSG(onDisk, c.file);
        AUREA_CHECK_MSG(loaded, c.file);
        AUREA_CHECK_MSG(title == c.title, c.file);
        AUREA_CHECK_MSG(content == c.title, c.file);
    }
    std::printf("    %u de 4 projetos Unicode voltaram iguais\n", same);
    std::filesystem::remove_all(dir, ec);
    AUREA_CHECK(!std::filesystem::exists(dir, ec));
}

// --- Cirílico na GPU real --------------------------------------------------------
#if defined(AUREA_TEST_VULKAN)

#include "VulkanBackend.hpp"

namespace {

bool i18n_vulkan_ok() {
    static const int ok = [] {
        vk::Backend b;
        BackendConfig cfg;
        cfg.enableValidation = false;
        const bool r = b.initialize(cfg).ok();
        if (r) b.shutdown();
        return r ? 1 : 0;
    }();
    return ok != 0;
}

/// Pixels acesos (qualquer canal > 8) de uma captura RGBA.
u32 lit_pixels(const std::vector<u8>& rgba) {
    u32 n = 0;
    for (usize i = 0; i + 3 < rgba.size(); i += 4) n += (rgba[i] > 8 || rgba[i + 1] > 8 || rgba[i + 2] > 8) ? 1u : 0u;
    return n;
}

} // namespace

// "Привет, мир" desenhado pelo motor (camada de texto → atlas SDF → quadro):
// o quadro vazio não tem pixel aceso; com o texto, tem — e o miolo das letras
// sai branco puro (glifo de verdade, não caixinha vazia).
AUREA_TEST(I18n, CyrillicTextLayerRendersOnGpu) {
    if (!i18n_vulkan_ok()) {
        std::printf("(sem GPU Vulkan: pulado) ");
        return;
    }
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(512, 288, 30.0, "Привет").ok());
    std::vector<u8> empty, withText;
    u32 w0 = 0, h0 = 0, w1 = 0, h1 = 0;
    AUREA_CHECK(e.capture_frame_rgba(512, empty, w0, h0).ok());
    auto id = e.add_text("Привет, мир");
    AUREA_CHECK(id.ok());
    AUREA_CHECK(e.capture_frame_rgba(512, withText, w1, h1).ok());
    const u32 litEmpty = lit_pixels(empty);
    const u32 litText = lit_pixels(withText);
    u32 white = 0;
    for (usize i = 0; i + 3 < withText.size(); i += 4) {
        white += (withText[i] > 245 && withText[i + 1] > 245 && withText[i + 2] > 245) ? 1u : 0u;
    }
    const f32 cover = static_cast<f32>(litText) / static_cast<f32>(std::max(1u, w1 * h1));
    std::printf("    cirilico na GPU: quadro %ux%u; vazio %u px acesos; com texto %u px (%.2f%%), %u brancos\n", w1, h1, litEmpty,
                litText, cover * 100.0f, white);
    AUREA_CHECK(w0 == w1 && h0 == h1 && w1 > 0);
    AUREA_CHECK(litEmpty == 0);
    AUREA_CHECK(litText > 500);
    AUREA_CHECK(cover < 0.5f);
    AUREA_CHECK(white > 50);
    e.shutdown();
}

#endif // AUREA_TEST_VULKAN
