// =============================================================================
//  Aurea / media / MediaRelink.hpp
//
//  1.0.8 — "A mídia original não está mais no aparelho" (erro 18) para sempre.
//  O projeto grava o caminho da mídia. Esse caminho deixa de valer quando:
//   - o iOS muda o UUID do contêiner numa atualização/reinstalação
//     (`…/Data/Application/<UUID>/…`): o arquivo continua no app, em outro
//     prefixo — e não só em Documents (Library/Application Support também);
//   - o Android grava o mesmo arquivo por `/data/data/<pacote>/` ou por
//     `/data/user/<n>/<pacote>/` (são a mesma pasta, mas o texto muda);
//   - a cópia foi parar em outra pasta do app (importação antiga, pacote,
//     backup restaurado) com o MESMO nome e tamanho.
//  Antes de mostrar qualquer erro, o motor procura o arquivo: primeiro o mesmo
//  relativo dentro do contêiner ATUAL, depois pelo nome (e tamanho, quando o
//  projeto o sabe) nas pastas do próprio app. Nunca sai do contêiner do app
//  (nada de outro pacote, nada de pasta de outro app), nunca escolhe entre dois
//  candidatos diferentes e nunca procura em pastas temporárias (o arquivo
//  achado ali sumiria de novo).
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace aurea::media_relink {

/// `stored` é um caminho de arquivo local (não URI `content:`/`http:`, não
/// receita `aurea-…:`, não `docs:`). Caminho absoluto POSIX ou do Windows.
[[nodiscard]] bool is_local_file_path(const std::string& stored) noexcept;

/// A raiz do contêiner do app que contém `documents`:
///  iOS     `…/Data/Application/<UUID>`
///  Android `/data/user/<n>/<pacote>` ou `/data/data/<pacote>`
/// Vazio quando `documents` não está num contêiner conhecido.
[[nodiscard]] std::string app_container_of(const std::string& documents) noexcept;

/// O caminho gravado por OUTRA instalação do MESMO app, trazido para o
/// contêiner atual (qualquer subpasta). Só quando o gravado não existe e o
/// novo é um arquivo comum. Android: só o mesmo pacote. Vazio = não se aplica.
[[nodiscard]] std::string rebase_into_container(const std::string& stored, const std::string& documents) noexcept;

/// As pastas do app onde a mídia de projeto pode morar: a de documentos e,
/// quando ela é `files/projetos` (Android) ou `Documents` (iOS), as irmãs
/// duráveis (`files/aurea-ai`, `Library/Application Support`). Sem caches nem tmp.
[[nodiscard]] std::vector<std::string> search_roots(const std::string& documents) noexcept;

/// Índice nome → arquivos (uma varredura limitada por chamada de abertura).
class NameIndex {
public:
    /// Varre `roots` (profundidade e quantidade limitadas; não segue links;
    /// pula tmp/caches/cópias parciais). Pode ser chamado de novo (refaz).
    void build(const std::vector<std::string>& roots, u32 maxEntries = 40000, u32 maxDepth = 8) noexcept;
    /// O único arquivo com um destes nomes (e `bytes`, quando > 0). Vários
    /// com o mesmo tamanho = cópias do mesmo conteúdo: o primeiro em ordem.
    /// Sem tamanho e mais de um candidato = ambíguo: vazio.
    [[nodiscard]] std::string find(const std::vector<std::string>& names, u64 bytes) const noexcept;
    [[nodiscard]] bool empty() const noexcept { return byName_.empty(); }
    [[nodiscard]] usize files() const noexcept { return files_; }

private:
    struct Entry { std::string path; u64 bytes = 0; };
    std::unordered_map<std::string, std::vector<Entry>> byName_;   ///< nome em minúsculas
    usize files_ = 0;
};

/// O nome do arquivo (depois da última barra, `/` ou `\`).
[[nodiscard]] std::string file_name_of(const std::string& path) noexcept;

/// O arquivo de agora para uma mídia gravada que não abre: contêiner atual
/// primeiro, depois o índice pelo nome do arquivo gravado e pelo nome original.
/// `index` nulo = só o contêiner. Vazio = continua ausente.
[[nodiscard]] std::string relocate(const std::string& stored, const std::string& documents, u64 bytes,
                                   const std::string& originalName, const NameIndex* index) noexcept;

} // namespace aurea::media_relink
