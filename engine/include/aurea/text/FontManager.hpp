// =============================================================================
//  Aurea / text / FontManager.hpp
//
//  As fontes do Aurea: as do aparelho (varridas uma vez, lendo SÓ as tabelas
//  `name` e `OS/2` de cada arquivo — nada de carregar 200 fontes inteiras) e
//  as importadas pelo usuário (TTF/OTF copiados para o projeto). Resolve a
//  fonte de uma camada de texto por caminho (importada), por família + peso +
//  itálico (sistema) e, na falta, pela fonte padrão — o projeto abre em outro
//  aparelho sem quebrar.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/text/Text.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace aurea {
struct TextData;
}

namespace aurea::text {

struct FontEntry {
    u32 id = 0;               ///< estável (hash do caminho)
    std::string family;       ///< "Roboto", "Noto Sans"…
    std::string style;        ///< "Regular", "Bold Italic"…
    std::string path;         ///< arquivo
    u16 weight = 400;         ///< 100..900 (OS/2)
    bool italic = false;
    bool imported = false;
    /// Omega conteúdo (área 06): categoria do navegador de fontes (FontCategory)
    /// e as escritas que a face cobre (bits FontScript), lidas do OS/2, do
    /// `post` e, sem faixas Unicode no OS/2, do `cmap`.
    u8  category = 0;
    u32 scripts = 0;
};

/// Categoria da face (FontEntry::category).
enum FontCategory : u8 { kFontSans = 0, kFontSerif = 1, kFontDisplay = 2, kFontHandwriting = 3, kFontMono = 4 };
/// Escritas (FontEntry::scripts, bits).
enum FontScript : u32 {
    kScriptLatin = 1u << 0, kScriptGreek = 1u << 1, kScriptCyrillic = 1u << 2, kScriptHebrew = 1u << 3,
    kScriptArabic = 1u << 4, kScriptThai = 1u << 5, kScriptKorean = 1u << 6, kScriptJapanese = 1u << 7,
    kScriptChinese = 1u << 8, kScriptDevanagari = 1u << 9,
};

/// Lê família, estilo, peso e itálico de um TTF/OTF/TTC (1ª face) sem
/// carregar o arquivo inteiro. Falso se não for uma fonte legível.
bool read_font_info(const std::string& path, FontEntry& out);

/// Mais formatos na importação (0.1.8, FontUnpack.cpp): WOFF vira .ttf/.otf e
/// cada face de uma coleção TTC/OTC vira um arquivo próprio, ao lado do
/// original. TTF/OTF voltam como estão; vazio = formato não aceito.
std::vector<std::string> unpack_font_file(const std::string& path);

class FontManager {
public:
    static FontManager& instance();

    /// Pastas de fontes do sistema (varridas na 1ª consulta). Vazio = padrão
    /// da plataforma (/system/fonts, C:/Windows/Fonts, /System/Library/Fonts).
    void set_system_dirs(std::vector<std::string> dirs);
    /// Pastas onde o app guarda as fontes IMPORTADAS (Android: docs/fontes,
    /// iOS: docs/Media). Varridas junto com as do sistema: a fonte importada
    /// volta à lista depois de fechar e abrir o app, não só no projeto.
    void set_imported_dirs(std::vector<std::string> dirs);
    /// Caminho guardado ("docs:…") → caminho real (o motor passa o dele).
    void set_path_resolver(std::function<std::string(const std::string&)> fn);

    /// Todas as fontes (sistema + importadas), por família e peso.
    [[nodiscard]] std::vector<FontEntry> list();
    /// Registra um arquivo de fonte (importado). Nulo se não for fonte.
    [[nodiscard]] const FontEntry* add_file(const std::string& path, bool imported);
    /// Omega conteúdo (área 06): tira uma fonte IMPORTADA da lista e do cache e
    /// apaga o arquivo. Falso se o caminho não é de uma importada.
    bool remove_imported(const std::string& path);
    /// Caminho guardado na camada ("docs:…") → caminho real (o resolvedor).
    [[nodiscard]] std::string resolve(const std::string& stored);

    /// Fonte de uma camada de texto (caminho → família/peso/itálico → padrão).
    [[nodiscard]] std::shared_ptr<const Font> font_for(const TextData& t);
    /// Fonte de um arquivo (cache).
    [[nodiscard]] std::shared_ptr<const Font> load(const std::string& path);

private:
    void scan_locked();
    std::mutex mutex_;
    bool scanned_ = false;
    std::vector<std::string> dirs_;
    std::vector<std::string> importedDirs_;
    std::vector<FontEntry> entries_;
    std::unordered_map<std::string, std::shared_ptr<const Font>> cache_;
    std::function<std::string(const std::string&)> resolver_;
};

} // namespace aurea::text
