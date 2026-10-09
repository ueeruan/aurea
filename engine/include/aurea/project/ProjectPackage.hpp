// =============================================================================
//  Aurea / project / ProjectPackage.hpp
//
//  "Exportar arquivo do projeto" / "Importar arquivo do projeto": o projeto
//  inteiro num ARQUIVO SÓ (`.aureaproj`) para mandar a outra pessoa, a outro
//  aparelho ou guardar fora do app.
//
//  O pacote é um ZIP sem compressão (vídeo e foto já vêm comprimidos; o
//  `.aurea` também): qualquer descompactador abre, e o app lê sem biblioteca.
//
//      manifest.txt        "aurea-project <versão>" + título, app, mídias
//      project.aurea       o projeto, byte a byte
//      media/NNN-nome.ext  a mídia (opção "Incluir mídia")
//
//  Importar NUNCA sobrescreve: quem chama escolhe um caminho livre para o
//  `.aurea` e uma pasta nova para a mídia. A mídia que veio no pacote é
//  religada no projeto (caminho novo, deste aparelho); a que não veio fica
//  contada em `missing` — a pessoa usa "Substituir mídia" nessas camadas.
//
//  Um `.aurea` solto também é aceito (sem mídia): é o mesmo projeto.
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/project/Asset.hpp"

#include <string>
#include <vector>

namespace aurea::package {

/// Versão do pacote que este app escreve. Maior no arquivo = feito por uma
/// versão mais nova do app → `UnsupportedVersion`.
inline constexpr u32 kPackageVersion = 1;
inline constexpr const char* kManifestEntry = "manifest.txt";
inline constexpr const char* kProjectEntry = "project.aurea";

/// Uma mídia que o projeto referencia (um asset com arquivo de origem).
struct MediaRef {
    AssetKind kind = AssetKind::Unknown;
    std::string stored;    ///< como está gravado no projeto
    std::string resolved;  ///< caminho legível neste aparelho (preenchido pelo Engine)
    std::string name;      ///< nome original, para a entrada do pacote
};

/// Mídias do `.aurea` (sem abrir no editor). Só o que cabe num arquivo: vídeo,
/// áudio, imagem, fonte, ambiente (HDRI), vetor e o arquivo do modelo 3D.
[[nodiscard]] Status list_media(const std::string& aureaPath, std::vector<MediaRef>& out) noexcept;

/// Uma mídia a incluir: `stored` é a chave (igual à do projeto), `readable`
/// é de onde ler agora (no Android pode ser `/proc/self/fd/N`).
struct MediaFile {
    std::string stored;
    std::string readable;
    std::string name;
};

struct ExportResult {
    u32 included = 0;
    u32 skipped = 0;       ///< pedida mas ilegível agora (fica de fora, sem falhar)
    u64 bytes = 0;
};

/// Escreve o pacote em `outPath` (temporário + rename: nunca um pacote pela
/// metade). `media` vazio = só o projeto.
[[nodiscard]] Status write_package(const std::string& aureaPath, const std::string& outPath,
                                   const std::string& title, const std::string& appVersion,
                                   const std::vector<MediaFile>& media, ExportResult* result = nullptr) noexcept;

struct ImportResult {
    std::string title;       ///< título gravado no pacote (vazio num `.aurea` solto)
    std::string appVersion;  ///< versão do app que exportou
    u32 relinked = 0;        ///< mídias religadas aos arquivos extraídos
    u32 missing = 0;         ///< mídias do projeto que não vieram no pacote
};

/// Lê o pacote (ou um `.aurea` solto): grava o projeto em `projectOut` (que
/// NÃO pode existir) e a mídia em `mediaDir` (criada; nomes únicos). Erros:
///   UnsupportedFormat   não é um projeto do Aurea
///   UnsupportedVersion  feito por uma versão mais nova do app
///   CorruptData         o projeto dentro não abre (ou o pacote está cortado)
///   IoError / StorageFull  arquivo ilegível / sem espaço
/// Em qualquer erro nada fica para trás (projeto e mídia extraída saem).
[[nodiscard]] Status read_package(const std::string& packagePath, const std::string& projectOut,
                                  const std::string& mediaDir, ImportResult& out) noexcept;

/// Extract model ZIPs (stored/deflate) into a NEW directory. Relative paths are
/// preserved; unsafe names, CRC errors and excessive memory/disk costs fail
/// without leaving partial files. Native pickers call the same implementation.
[[nodiscard]] Status extract_model_archive(const std::string& archivePath, const std::string& directory,
                                          std::vector<std::string>& files) noexcept;

/// Troca o caminho das mídias do `.aurea` (chave = caminho gravado). Devolve
/// quantos assets mudaram. Usado pelo `read_package`; exposto para teste.
[[nodiscard]] Status relink_media(const std::string& aureaPath,
                                  const std::vector<std::pair<std::string, std::string>>& storedToNew,
                                  u32* relinked = nullptr) noexcept;

} // namespace aurea::package
