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
};

/// Lê família, estilo, peso e itálico de um TTF/OTF/TTC (1ª face) sem
/// carregar o arquivo inteiro. Falso se não for uma fonte legível.
bool read_font_info(const std::string& path, FontEntry& out);

class FontManager {
public:
    static FontManager& instance();

    /// Pastas de fontes do sistema (varridas na 1ª consulta). Vazio = padrão
    /// da plataforma (/system/fonts, C:/Windows/Fonts, /System/Library/Fonts).
    void set_system_dirs(std::vector<std::string> dirs);
    /// Caminho guardado ("docs:…") → caminho real (o motor passa o dele).
    void set_path_resolver(std::function<std::string(const std::string&)> fn);

    /// Todas as fontes (sistema + importadas), por família e peso.
    [[nodiscard]] std::vector<FontEntry> list();
    /// Registra um arquivo de fonte (importado). Nulo se não for fonte.
    [[nodiscard]] const FontEntry* add_file(const std::string& path, bool imported);

    /// Fonte de uma camada de texto (caminho → família/peso/itálico → padrão).
    [[nodiscard]] std::shared_ptr<const Font> font_for(const TextData& t);
    /// Fonte de um arquivo (cache).
    [[nodiscard]] std::shared_ptr<const Font> load(const std::string& path);

private:
    void scan_locked();
    std::mutex mutex_;
    bool scanned_ = false;
    std::vector<std::string> dirs_;
    std::vector<FontEntry> entries_;
    std::unordered_map<std::string, std::shared_ptr<const Font>> cache_;
    std::function<std::string(const std::string&)> resolver_;
};

} // namespace aurea::text
