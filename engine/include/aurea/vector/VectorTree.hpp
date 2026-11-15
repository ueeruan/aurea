// =============================================================================
//  Aurea / vector / VectorTree.hpp
//
//  Lote X do AE (2026-11-12, gap_system item 26): o conteúdo de um grupo
//  vetorial como LISTA ORDENADA de itens, no molde da shape layer do After
//  Effects — caminho | preencher | contorno | operador (aparar, mesclar,
//  repetidor, operadores de caminho) | grupo — avaliada de CIMA para baixo:
//
//    * um operador vale para todos os caminhos ACIMA dele no grupo (e os dos
//      grupos de dentro que estão acima);
//    * um Preencher / Contorno pinta os caminhos acima dele, do jeito que os
//      operadores ACIMA dele os deixaram; quem está mais em cima na lista é
//      desenhado por cima;
//    * o Repetidor repete tudo o que está acima dele (caminhos e o que já foi
//      pintado, inclusive grupos de dentro);
//    * vários Preencher / Contorno / Aparar / Repetidor / Mesclar por grupo;
//    * grupos dentro de grupos, cada um com o seu transform (âncora, posição,
//      escala, inclinação + eixo, giro) e a sua opacidade.
//
//  O DADO continua no VectorGroup (vector/VectorData.hpp): o primeiro item de
//  cada tipo é o de sempre (`fill`, `stroke`, `trim`, `repeater`, `merge`,
//  `paths`, `modifiers`) — é o que as trilhas VectorParam animam e o que o
//  painel do celular edita — e os demais ficam em `more*` / `children`.
//  `VectorGroup::order` é a sequência de TIPOS, de cima para baixo: a n-ésima
//  entrada de um tipo é o n-ésimo item daquele tipo. Vazio = o grupo de
//  sempre, na ordem fixa de sempre (o caminho de código antigo, intocado).
//
//  Limites (ditos aqui para ninguém descobrir no susto):
//    * Mesclar e Aparar trabalham em contornos achatados: um operador de
//      caminho DEPOIS deles recebe polilinhas (Aspereza e Zigue-zague contam
//      os segmentos achatados, não os da curva original).
//    * Progresso do desenho não corta grupos de ordem livre (saem inteiros).
//    * Só os itens do primeiro nível animam por trilha (os de sempre); os de
//      grupos de dentro e os itens a mais ficam parados.
// =============================================================================
#pragma once

#include "aurea/vector/Vector.hpp"
#include "aurea/vector/VectorData.hpp"

#include <string>
#include <vector>

namespace aurea::vector {

/// O grupo usa a árvore (ordem livre, itens a mais, grupos de dentro ou
/// inclinação)? Falso = o grupo de sempre.
[[nodiscard]] bool group_has_tree(const VectorGroup& g) noexcept;
[[nodiscard]] bool data_has_tree(const VectorData& d) noexcept;

/// Quantos itens do tipo o grupo guarda (Preencher/Contorno/Aparar/Repetidor/
/// Mesclar: 1 + os a mais).
[[nodiscard]] u32 item_count(const VectorGroup& g, VectorItemKind kind) noexcept;

/// A ordem do grupo de sempre, escrita como lista: caminhos, operadores de
/// caminho, Mesclar, Aparar, Contorno, Preencher, Repetidor. Avaliar um grupo
/// com esta ordem dá EXATAMENTE a geometria do caminho antigo.
[[nodiscard]] std::vector<VectorItemKind> classic_order(const VectorGroup& g);
/// A ordem que vale: `g.order` consertada (entradas sem item saem; caminho,
/// operador e grupo sem entrada entram depois do último do mesmo tipo), ou a
/// de sempre quando `g.order` está vazia.
[[nodiscard]] std::vector<VectorItemKind> effective_order(const VectorGroup& g);
/// Passa o grupo para a ordem livre gravando a ordem que vale (não muda o desenho).
void materialize_order(VectorGroup& g);

/// Uma camada de tinta pronta: anéis limpos no espaço local dela, o afim até
/// o espaço do grupo de primeiro nível (ANTES do transform dele), a tinta e a
/// opacidade (0..1, sem a do grupo de primeiro nível).
struct TreePaint {
    std::vector<Contour> rings;
    Affine2 M{};
    u32 paint = 0;          ///< índice em TreeResult::paints
    f32 alpha = 1.0f;
};
struct TreeResult {
    std::vector<TreePaint> layers;      ///< em ordem de desenho: a primeira fica por baixo
    std::vector<VectorPaint> paints;
};
/// Avalia o grupo (já com as trilhas aplicadas) pela ordem que vale, no
/// espaço do grupo. `tolerance` em px do grupo.
void evaluate_tree(const VectorGroup& g, f64 frame, f32 tolerance, TreeResult& out);
/// Maior escala que os repetidores do grupo (e dos de dentro) dão a uma cópia
/// (≥ 1, com teto): entra na tolerância do achatamento.
[[nodiscard]] f32 tree_repeat_scale(const VectorGroup& g) noexcept;
/// Caixa aproximada (espaço da camada) de um grupo com árvore. Falso = vazio.
bool tree_bounds(const VectorGroup& g, f64 frame, Vec2& mn, Vec2& mx);
/// Chave de conteúdo do que a árvore acrescenta ao grupo (ordem, itens a
/// mais, grupos de dentro, inclinação). 0 quando o grupo não usa a árvore.
[[nodiscard]] u64 tree_hash(const VectorGroup& g, f64 frame) noexcept;

// --- Documento da árvore --------------------------------------------------------
//
// O fluxo de sempre (encode_document, versão 6) continua igual, byte a byte:
// é o que os painéis do celular leem, e eles recusam versão desconhecida. O
// que a árvore acrescenta vai num fluxo À PARTE (versão própria), gravado na
// seção VectorTree do projeto:
//   árvore : versão (1), nº de grupos, extras…
//   extras : inclinação, eixo, nº de preencher a mais, preencher…, nº de
//            contornos a mais, contorno…, nº de aparar a mais, aparar…,
//            nº de repetidores a mais, repetidor…, nº de mesclar a mais,
//            modo…, nº de entradas da ordem, tipo…, nº de grupos de dentro,
//            (nome: nº de bytes, bytes…; grupo no formato de sempre; extras)…
void encode_tree(const VectorData& data, std::vector<f32>& out);
/// Aplica os extras sobre `data` (os grupos de primeiro nível já lidos do
/// fluxo de sempre). Falso = fluxo curto/corrompido ou contagem diferente:
/// nada muda.
bool decode_tree(const f32* data, usize count, VectorData& into);
/// Copia de `from` para `into` só o que é da árvore (inclinação, itens a mais,
/// grupos de dentro, ordem): quem refaz um grupo a partir do documento de
/// sempre (o painel do celular gravando) não perde o resto.
void keep_tree(const VectorGroup& from, VectorGroup& into);

// --- Listagem e edição (o painel em árvore do Aurea PC) -----------------------------

/// Campos de um item na listagem / em `set_item_values`, por tipo:
///   Caminho  : tipo (VectorPathKind), centro x/y, tamanho x/y, arredondar,
///              pontas, raio externo, raio interno, giro
///   Preencher: r, g, b, opacidade %, regra
///   Contorno : r, g, b, opacidade %, largura, ponta, junta, miter
///   Aparar   : início %, fim %, deslocamento %, modo
///   Repetidor: cópias, deslocamento, posição x/y, escala X %, escala Y %,
///              giro, opacidade inicial %, final %, por cima, âncora x/y
///   Mesclar  : modo (0 nenhum, 1 unir, 2 subtrair, 3 interseção, 4 excluir)
///   Operador : tipo, 16 parâmetros
///   Grupo    : posição x/y, âncora x/y, escala x/y %, giro, opacidade %,
///              inclinação, eixo da inclinação
inline constexpr u32 kTreeMaxValues = 17;
/// Uma linha da árvore.
struct TreeRow {
    u32 depth = 0;                 ///< 0 = grupo de primeiro nível
    VectorItemKind kind = VectorItemKind::Group;
    u32 ordinal = 0;               ///< n-ésimo item do tipo no grupo dono
    u32 position = 0;              ///< posição na ordem do grupo dono (de cima para baixo)
    bool enabled = true;
    u32 valueCount = 0;
    f32 values[kTreeMaxValues]{};
    std::string name;              ///< só grupos
};
/// Todas as linhas, em pré-ordem (o grupo, depois o conteúdo dele de cima para baixo).
void list_tree(const VectorData& data, std::vector<TreeRow>& out);

/// O grupo no caminho de índices (primeiro nível, filho, neto…); nulo = não existe.
[[nodiscard]] VectorGroup* group_at(VectorData& data, const u32* path, u32 depth) noexcept;
[[nodiscard]] const VectorGroup* group_at(const VectorData& data, const u32* path, u32 depth) noexcept;

/// De onde para onde foram os itens de UM tipo depois de uma edição: `map[i]`
/// = novo índice do item que era `i` (kInvalidIndex = saiu). Quem guarda
/// trilhas por índice (caminhos e operadores do primeiro nível) remapeia.
struct TreeRemap {
    VectorItemKind kind = VectorItemKind::Path;
    std::vector<u32> map;
    bool changed = false;
};
/// Acrescenta um item do tipo na posição `position` da ordem (≥ tamanho = no
/// fim). `variant`: tipo do caminho (VectorPathKind) ou do operador
/// (VectorModifierType); ignorado nos outros. Passa o grupo para a ordem livre.
bool add_item(VectorGroup& g, VectorItemKind kind, u32 position, u32 variant, const std::string& name, TreeRemap* remap = nullptr);
bool remove_item(VectorGroup& g, u32 position, TreeRemap* remap = nullptr);
bool move_item(VectorGroup& g, u32 from, u32 to, TreeRemap* remap = nullptr);
bool set_item_enabled(VectorGroup& g, u32 position, bool enabled);
bool set_item_values(VectorGroup& g, u32 position, const f32* values, u32 count);

} // namespace aurea::vector
