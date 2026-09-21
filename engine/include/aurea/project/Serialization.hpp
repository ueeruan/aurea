// =============================================================================
//  Aurea / project / Serialization.hpp
//
//  O formato .aurea.
//
//  Um arquivo .aurea é um CONTÊINER de seções, não um blob monolítico:
//
//     manifest      — versão, checksum, índice de seções
//     project       — metadados, export settings, editor settings
//     timeline      — composições e layers (a seção grande)
//     animations    — keyframes e curvas, separados das layers
//     assets        — metadados de mídia (não a mídia em si)
//     effects       — cadeias de efeitos por layer
//     scene3d       — cenas, materiais, luzes, câmeras
//     particles     — sistemas de partícula
//     fonts         — referências e métricas de fonte
//     thumbnails    — miniatura do projeto, para a Home
//
//  Por que separar: gravar incrementalmente exige saber o que mudou. Com seções
//  independentes, mexer numa animação reescreve só a seção `animations` e
//  atualiza o índice — o resto do arquivo não é tocado. Num projeto de dezenas
//  de MB, isso é a diferença entre 5 ms e 400 ms por autosave.
//
//  Versionamento: a versão vai no manifest E no cabeçalho de CADA seção. Assim,
//  uma seção de formato antigo é migrada individualmente em vez de o arquivo
//  inteiro ser recusado. Um projeto antigo abre, e o que foi migrado é
//  registrado — nunca se abre "quase tudo" silenciosamente.
//
//  Integridade: cada seção carrega um checksum. Uma seção corrompida é
//  detectada e reportada; o resto do projeto abre. É o que permite ao usuário
//  salvar o trabalho depois de um crash no meio de uma escrita.
// =============================================================================
#pragma once

#include "aurea/project/Project.hpp"
#include "aurea/core/Result.hpp"

#include <functional>
#include <string>
#include <vector>

namespace aurea {

/// Seções do arquivo. O valor é contrato de arquivo: nunca reordene.
enum class SectionKind : u16 {
    Manifest    = 0,
    Project     = 1,
    Timeline    = 2,
    Animations  = 3,
    Assets      = 4,
    Effects     = 5,
    Scene3D     = 6,
    Particles   = 7,
    Fonts       = 8,
    Thumbnails  = 9,
    _Count,
};

/// Cabeçalho de uma seção no arquivo.
struct SectionHeader {
    SectionKind kind = SectionKind::Manifest;
    u32 version = 0;
    u64 offset  = 0;
    u64 size    = 0;          ///< bytes comprimidos
    u64 rawSize = 0;          ///< bytes descomprimidos
    u32 crc32   = 0;
    u32 flags   = 0;          ///< bit 0 = comprimido (deflate)
};

/// Cabeçalho do arquivo.
struct FileHeader {
    /// 'AURE' — recusa qualquer coisa que não seja um projeto do Aurea.
    static constexpr u32 kMagic = 0x41455255;
    /// Versão do FORMATO, não do app. Muda quando o layout dos bytes muda.
    static constexpr u16 kCurrentFormatVersion = 1;
    /// Versão mínima que consegue ler este arquivo. Se o leitor for mais antigo,
    /// recusa com mensagem clara em vez de abrir errado.
    static constexpr u16 kMinReaderVersion = 1;

    u32 magic = kMagic;
    u16 formatVersion = kCurrentFormatVersion;
    u16 minReaderVersion = kMinReaderVersion;
    u32 sectionCount = 0;
    u64 indexOffset = 0;
    u64 totalSize = 0;
    /// Versão do app que gravou, em texto curto ("2.0.0-beta1").
    char appVersion[32]{};
};

/// Migração de uma seção de formato antigo. Devolve ok se migrou (ou se já
/// estava na versão corrente). Devolve `UnsupportedVersion` se não há caminho —
/// e nesse caso o arquivo é recusado, não aberto parcialmente.
using SectionMigrationFn = Status (*)(SectionKind kind, u32 fromVersion,
                                      const u8* data, usize size,
                                      std::vector<u8>& out);

/// Opções de gravação.
struct SaveOptions {
    bool compress = true;             ///< deflate nas seções grandes
    bool incremental = false;         ///< aproveita seções inalteradas do arquivo existente
    bool writeThumbnail = true;
    bool fsyncOnComplete = true;      ///< garante que os bytes chegaram ao disco

    /// Seções que mudaram desde a última gravação. Vazio + incremental = grava
    /// tudo. O chamador marca o que mudou (ex.: só mexeu em animação).
    u64 changedSections = 0;
};

/// Opções de leitura.
struct LoadOptions {
    /// Só lê metadados e miniatura. É o que a Home usa para desenhar a lista de
    /// projetos sem carregar 40 MB por cartão.
    bool metadataOnly = false;
    /// Tolerar seções corrompidas: abre o resto e reporta. Usado na recuperação
    /// pós-crash, onde algo é melhor que nada.
    bool tolerateCorruptSections = false;
    /// Não carrega assets pesados (metadados só). A mídia é resolvida sob
    /// demanda, quando uma layer a referencia.
    bool lazyAssets = true;
};

/// Resultado de uma leitura, com o que foi feito.
struct LoadReport {
    std::vector<SectionKind> sectionsRead;
    std::vector<SectionKind> sectionsMigrated;
    std::vector<SectionKind> sectionsCorrupt;
    std::vector<SectionKind> sectionsSkipped;
    std::string warning;    ///< texto para a UI quando algo foi degradado
    bool partial = false;   ///< true = abriu com seções faltando

    [[nodiscard]] bool clean() const noexcept {
        return sectionsCorrupt.empty() && sectionsMigrated.empty() && !partial;
    }
};

/// Serializador do projeto.
///
/// Sem estado entre chamadas de propósito: gravar o arquivo A e ler o arquivo B
/// não compartilham nada, então o autosave pode rodar numa worker thread
/// enquanto a UI continua mexendo no projeto em memória.
class ProjectSerializer {
public:
    // --- Escrita --------------------------------------------------------------

    /// Grava o projeto. Escrita ATÔMICA: escreve num temporário, fsync, e
    /// renomeia por cima. Uma queda no meio deixa o arquivo antigo intacto —
    /// nunca um .aurea pela metade.
    [[nodiscard]] static Status save(const Project& project, const std::string& path,
                                     const SaveOptions& options,
                                     std::string* outError = nullptr);

    /// Grava só o journal de comandos. É o autosave incremental: barato o
    /// bastante para rodar a cada poucos segundos.
    [[nodiscard]] static Status append_journal(const std::string& journalPath,
                                               const Command* commands, u32 count,
                                               const char* stringBlob, u32 stringBlobSize);

    /// Lê o journal e devolve os comandos para reaplicar.
    [[nodiscard]] static Status read_journal(const std::string& journalPath,
                                             std::vector<Command>& outCommands);

    // --- Leitura --------------------------------------------------------------

    [[nodiscard]] static Status load(Project& out, const std::string& path,
                                     const LoadOptions& options,
                                     LoadReport* outReport = nullptr,
                                     std::string* outError = nullptr);

    /// Lê só o cabeçalho e o índice de seções, sem descomprimir nada. A Home
    /// usa isto para validar um arquivo antes de oferecê-lo como "abrir".
    [[nodiscard]] static Status peek(const std::string& path,
                                     FileHeader& outHeader,
                                     std::vector<SectionHeader>& outSections,
                                     std::string* outError = nullptr);

    // --- Migração -------------------------------------------------------------

    /// Registra uma migração para uma seção. Chamado uma vez na inicialização,
    /// com as funções que sabem converter versões antigas.
    static void register_migration(SectionKind kind, u32 fromVersion,
                                   SectionMigrationFn fn) noexcept;

    // --- Não implementado, declarado explicitamente ---------------------------
    //
    //  O formato da versão 1 concentra tudo numa seção por vez. A gravação
    //  incremental de verdade (aproveitar bytes de seções inalteradas do
    //  arquivo existente sem reescrevê-las) exige um gerenciador de blocos
    //  livres e um log de transações — está planejado para a versão 2 do
    //  formato e NÃO está implementado. Quando `SaveOptions::incremental` é
    //  pedido hoje, a gravação é completa e o fato é registrado no relatório.
    [[nodiscard]] static bool incremental_save_implemented() noexcept { return false; }
};

} // namespace aurea
