// =============================================================================
//  Aurea / project / MediaStorage.hpp
//
//  O que pode sair do armazenamento do app sem levar nada de que a pessoa ainda
//  precisa. Regra única para Android e iOS (as telas só pedem e mostram):
//
//   - Só arquivos DENTRO das pastas de mídia do app (`managedRoots`) podem ser
//     devolvidos ou apagados. Galeria, Documentos do sistema, `content://`,
//     URL com escopo de segurança, outro app: nunca. As sub-pastas de
//     biblioteca (`keepRoots`: modelos, LUTs, fontes) também nunca.
//   - Referência é o que QUALQUER arquivo de projeto restaurável aponta: o
//     `.aurea`, o `.bak`, o `.tmp` (queda entre o fsync e o rename), as cópias
//     de formato antigo (`.vN.bak`), o `.corrompido` e o `.recusado`. Um só
//     ilegível = nada sai (o projeto em si continua apagável normalmente).
//   - Projeto aberto (e o export em andamento) conta como referência.
//   - Pasta DERIVADA (PSD/GIF expandido, .zip de sequência extraído, quadros
//     soltos copiados; marcador `kDerivedMarker`) sai inteira ou não sai: a
//     próxima importação da mesma fonte reaproveita a pasta e não pode achá-la
//     pela metade. As do formato antigo (`PSD-<n>`, `GIF-<n>`, `seq-<n>`, sem
//     marcador) seguem a mesma regra, sem nunca serem reaproveitadas; a cópia
//     interrompida (`*.parcial`) sai inteira como órfã.
//   - Mídia já órfã (de versões antigas) NUNCA sai sozinha: `scan` a mostra e
//     só um pedido explícito da pessoa a apaga.
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace aurea::storage {

/// Arquivo dentro de uma pasta que nasceu inteira de UMA fonte (PSD, GIF,
/// .zip de sequência). Gravado só depois do último arquivo da pasta.
inline constexpr const char* kDerivedMarker = ".aurea-derived";

/// Caminho gravado no projeto → caminho deste aparelho (o Engine resolve
/// "docs:", o contêiner antigo do iOS e o prefixo antigo do Android).
using Resolver = std::function<std::string(const std::string& stored)>;

/// O arquivo é uma cópia restaurável de um projeto (ou o próprio `.aurea`)?
/// `.aurea`, `.aurea.bak`, `.aurea.tmp`, `.aurea.vN.bak`, `.aurea.corrompido[N]`,
/// `.aurea.recusado`. O sidecar `.aurea.meta.json` não é.
[[nodiscard]] bool is_project_file(const std::string& fileName) noexcept;

/// Todos os arquivos de projeto (com as cópias) dentro de `directory`, em
/// qualquer profundidade. Pastas ocultas e links ficam de fora.
[[nodiscard]] Status find_project_files(const std::string& directory, std::vector<std::string>& out) noexcept;

/// Caminho absoluto, normalizado e com os links resolvidos (vazio = não é um
/// caminho de arquivo deste aparelho, ex.: `content://`).
[[nodiscard]] std::string canonical_file_path(const std::string& path) noexcept;

/// Referências já lidas (caminhos canônicos), com o tipo de cada uma.
struct MediaRefs {
    std::vector<std::string> paths;
    std::vector<u8> kinds;   ///< AssetKind de cada caminho
};

/// Lê as mídias de um arquivo de projeto (estrito: seção faltando = erro).
[[nodiscard]] Status read_project_refs(const std::string& projectFile, const Resolver& resolve, MediaRefs& out) noexcept;

struct ReleaseRequest {
    std::vector<std::string> deletedProjects;   ///< arquivos de projeto que vão ser apagados (com as cópias)
    std::vector<std::string> remainingProjects; ///< TODOS os outros arquivos de projeto (com as cópias)
    MediaRefs live;                             ///< projeto aberto / export em andamento
    std::vector<std::string> managedRoots;
    std::vector<std::string> keepRoots;
};

struct ReleasePlan {
    std::vector<std::string> paths;  ///< arquivos (ou pastas derivadas inteiras) só dos projetos apagados
    u64 bytes = 0;
    u32 unreadable = 0;              ///< arquivos de projeto que ficam e não abriram
    bool blocked = false;            ///< true = nada sai (ilegível, export em andamento)
};

/// A mídia que SÓ os projetos apagados usam. Só vídeo e imagem: fonte, LUT,
/// modelo 3D, HDRI e vetor são biblioteca, e o áudio importado é a biblioteca
/// de sons do app. Lê tudo antes de qualquer arquivo ser apagado: chame com os
/// projetos apagados ainda no disco.
[[nodiscard]] Status plan_release(const ReleaseRequest& request, const Resolver& resolve, ReleasePlan& out) noexcept;

struct ScanRequest {
    std::vector<std::string> projects;   ///< todos os arquivos de projeto (com as cópias)
    MediaRefs live;
    std::vector<std::string> managedRoots;
    std::vector<std::string> keepRoots;
    /// Mais novo que isto nunca é órfão: uma importação ainda não gravada no projeto.
    u64 minAgeSeconds = 24ull * 3600;
    /// false = só vídeo, imagem e arquivo sem extensão (áudio é a biblioteca de
    /// sons: nunca órfão); true = qualquer arquivo (varredura de temporários).
    bool anyFile = false;
};

struct ScanResult {
    u64 referencedBytes = 0, orphanBytes = 0;
    u32 referencedFiles = 0, orphanFiles = 0;
    std::vector<std::string> orphans;    ///< arquivos (ou pastas derivadas) que nenhum projeto usa
    u32 unreadable = 0;
    bool blocked = false;                ///< true = projeto ilegível: nada é órfão (tudo conta como usado)
};

/// Quanto as pastas de mídia ocupam e o que nenhum projeto usa. Só leitura.
[[nodiscard]] Status scan(const ScanRequest& request, const Resolver& resolve, ScanResult& out) noexcept;

struct RemoveResult {
    u64 bytes = 0;
    u32 files = 0;
    u32 refused = 0;   ///< pedidos fora das pastas permitidas (ou link): ignorados
};

/// Apaga os caminhos da lista que CONTINUAM dentro de `managedRoots` (e fora de
/// `keepRoots`): arquivo comum, ou pasta derivada inteira (com o marcador, sem
/// link dentro). Pastas que ficam vazias saem, até a raiz (a raiz fica).
[[nodiscard]] RemoveResult remove_managed(const std::vector<std::string>& paths,
                                          const std::vector<std::string>& managedRoots,
                                          const std::vector<std::string>& keepRoots) noexcept;

/// Chave do CONTEÚDO de um arquivo (tamanho + hash de todos os bytes, quatro
/// faixas FNV-1a): o mesmo arquivo copiado para outro lugar dá a mesma chave.
[[nodiscard]] Status file_content_key(const std::string& path, u64& hash, u64& bytes) noexcept;
/// A mesma chave lida de um arquivo já aberto, do ponto atual até o fim (o
/// Android abre a URI escolhida e passa o descritor). Não fecha `file`.
[[nodiscard]] Status stream_content_key(std::FILE* file, u64& hash, u64& bytes) noexcept;
[[nodiscard]] u64 content_hash(const u8* data, usize size) noexcept;

/// Marca a pasta como derivada (depois do último arquivo dela).
[[nodiscard]] bool mark_derived(const std::string& directory) noexcept;
[[nodiscard]] bool is_derived(const std::string& directory) noexcept;

/// Pasta do formato antigo, gravada inteira de uma fonte e sem marcador:
/// `Media/PSD-<n>`, `Media/GIF-<n>` (PSD/GIF expandidos até a 0.5.1) e
/// `Media/sequences/seq-<n>` (quadros soltos copiados). Só arquivos comuns
/// dentro, sem sub-pasta. Ela é tratada como a derivada (sai inteira, só
/// quando nenhum arquivo restaurável aponta para dentro, e só pelo pedido da
/// pessoa ou com o projeto apagado) e nunca é movida nem reaproveitada.
[[nodiscard]] bool is_legacy_derived(const std::string& directory) noexcept;

// --- Sequência de quadros SOLTOS ---------------------------------------------
// As plataformas copiam os quadros escolhidos (URI, cópia do seletor) para
// dentro do app. A pasta tem o nome da chave do CONJUNTO (nome + conteúdo de
// cada quadro): a mesma seleção importada de novo acha a pasta pronta e não
// copia nada. A cópia vai para `<pasta>.parcial`; só depois do último quadro
// conferido ela ganha o marcador e o nome final (queda no meio = nunca
// reaproveitada; a sobra sai na próxima importação igual ou como órfã).

/// Um quadro: o nome que ele terá na pasta e a chave do conteúdo da fonte.
struct FrameKey {
    std::string name;
    u64 hash = 0;
    u64 bytes = 0;
};

/// `seq-<hash do conjunto>-<bytes>`. Vazio = nome inválido (separador, oculto,
/// o marcador) ou repetido.
[[nodiscard]] std::string frame_folder_name(const std::vector<FrameKey>& frames);

struct FrameFolderPlan {
    std::string directory;   ///< reuse: a pasta pronta; senão, onde a cópia vai parar
    std::string staging;     ///< vazio quando reuse; senão a pasta (vazia) para copiar
    bool reuse = false;
};

/// Antes de copiar: a pasta pronta com estes quadros (tamanhos conferidos) ou
/// a `.parcial` limpa onde copiar. Uma pasta com o nome da chave mas sem
/// marcador (ou com outro conteúdo) nunca é usada nem apagada: o próximo
/// nome livre (`-2`, `-3`…) recebe a cópia.
[[nodiscard]] Status plan_frame_folder(const std::string& sequencesRoot, const std::vector<FrameKey>& frames,
                                       FrameFolderPlan& out) noexcept;

/// Depois da cópia: relê cada arquivo de `staging` (a fonte pode ter mudado
/// entre a chave e a cópia), dá o nome da chave do que foi COPIADO e marca a
/// pasta. Já existindo a pronta com esse conteúdo, a cópia sai e a pronta é
/// devolvida (`reused`). `staging` precisa ser uma `.parcial` direto em
/// `sequencesRoot`; qualquer erro apaga a cópia.
[[nodiscard]] Status commit_frame_folder(const std::string& sequencesRoot, const std::string& staging,
                                         std::string& directory, bool& reused) noexcept;

/// Cancelamento ou falha da cópia: apaga a `.parcial` (só uma `.parcial` direto
/// em `sequencesRoot`; outra coisa é recusada).
bool discard_frame_folder(const std::string& sequencesRoot, const std::string& staging) noexcept;

} // namespace aurea::storage
