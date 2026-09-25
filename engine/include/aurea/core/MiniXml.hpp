// =============================================================================
//  Aurea / core / MiniXml.hpp
//
//  XML PEQUENO E PERMISSIVO. O motor lê dois XML de fora — SVG e a cena do
//  Alight Motion — e nenhum dos dois precisa de validação, namespace ou DTD:
//  precisa de tag, atributos e filhos, sem cair com arquivo estranho.
//
//  O que o leitor faz: elementos, atributos entre aspas simples ou duplas
//  (entidades básicas decodificadas), comentários, CDATA, <?...?> e DOCTYPE
//  pulados; prefixo de namespace fora da tag (svg:path → path). Texto entre
//  tags é ignorado. Fechamento fora de ordem fecha o elemento de cima.
//  Maiúsculas ficam como vieram (o SVG distingue viewBox de viewbox); quem
//  lê um formato sem essa distinção usa `attr_nocase`.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aurea::xml {

struct Node {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<Node> kids;

    /// Atributo pelo nome exato (nulo se não houver).
    [[nodiscard]] const std::string* attr(const char* n) const;
    /// Atributo sem diferenciar maiúsculas (startTime = starttime).
    [[nodiscard]] const std::string* attr_nocase(std::string_view n) const;
};

/// &amp; &lt; &gt; &quot; &apos; → caractere.
[[nodiscard]] std::string decode_entities(const std::string& s);

/// Lê `s` numa árvore sob `root` (raiz sem nome). Falso = nenhum elemento.
/// `maxDepth` > 0 recusa (falso) aninhamento mais fundo que isso — arquivo
/// hostil não vira árvore de um milhão de níveis. 0 = sem limite.
[[nodiscard]] bool parse(const std::string& s, Node& root, u32 maxDepth = 0);

/// Compara sem diferenciar maiúsculas (ASCII).
[[nodiscard]] bool iequals(std::string_view a, std::string_view b) noexcept;

} // namespace aurea::xml
