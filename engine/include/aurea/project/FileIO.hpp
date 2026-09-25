// =============================================================================
//  Aurea / project / FileIO.hpp
//
//  Escrita de arquivo que não perde o trabalho do usuário (§53–58, §118).
//
//  `write_atomic`: temporário ao lado do destino → fwrite → fflush → fsync →
//  fclose (os três CHECADOS: disco cheio costuma aparecer só no flush ou no
//  close, com o fwrite dizendo que "escreveu") → cópia de segurança do arquivo
//  anterior (.bak) → rename atômico por cima. Qualquer falha antes do rename
//  deixa o arquivo antigo byte a byte intacto e apaga o temporário.
//
//  O `.bak` guarda a versão ANTERIOR, sem janela em que o principal some:
//  POSIX faz hard link (cai para cópia se o sistema recusar), Windows usa
//  ReplaceFileW. É o "último estado válido" que o leitor usa quando o
//  principal está corrompido.
//
//  Injeção de falha: os testes simulam disco cheio, flush recusado e rename
//  recusado sem encher o disco de verdade. Global e sem custo quando desligada
//  (um load atômico relaxado por escrita).
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace aurea::fileio {

/// `fopen` com caminho UTF-8 em toda plataforma. No Android/iOS o `char*` já
/// é UTF-8 e isto é o `fopen` de sempre; no Windows o `fopen` estreito lê o
/// caminho como ANSI (código de página do sistema) e um nome em árabe, hindi,
/// russo ou com emoji simplesmente não abre — lá vai por `_wfopen` (UTF-16).
[[nodiscard]] std::FILE* open_file(const std::string& path, const char* mode) noexcept;
/// `remove` com caminho UTF-8 (mesma regra do `open_file`). true = apagou.
bool remove_file(const std::string& path) noexcept;

struct AtomicWriteOptions {
    bool fsync = true;         ///< fsync no arquivo e (POSIX) na pasta depois do rename
    bool keepBackup = false;   ///< o arquivo anterior vira `<path>.bak`
};

/// Grava `data` em `path` atomicamente. Erros: StorageFull (ENOSPC/EDQUOT),
/// IoError (o resto). `outError` recebe o texto para o log (sem o conteúdo).
[[nodiscard]] Status write_atomic(const std::string& path, const void* data, usize size,
                                  const AtomicWriteOptions& options = {},
                                  std::string* outError = nullptr) noexcept;

/// Lê o arquivo inteiro. false = não existe, ilegível ou maior que `maxBytes`.
[[nodiscard]] bool read_all(const std::string& path, std::vector<u8>& out, usize maxBytes) noexcept;

/// Copia `from` para `to` (atômico no destino). Usado para a cópia de
/// recuperação antes de regravar um projeto de formato antigo.
[[nodiscard]] Status copy_file(const std::string& from, const std::string& to) noexcept;

[[nodiscard]] bool exists(const std::string& path) noexcept;
[[nodiscard]] inline std::string backup_path(const std::string& path) { return path + ".bak"; }
[[nodiscard]] inline std::string temp_path(const std::string& path) { return path + ".tmp"; }

// --- Injeção de falha (testes) ------------------------------------------------
enum class Fault : u8 {
    None = 0,
    OpenFails,          ///< o temporário não abre (pasta sem permissão)
    DiskFullAfter,      ///< ENOSPC depois de `afterBytes` bytes
    FlushFails,         ///< fflush/fsync falham com ENOSPC (o caso real mais comum)
    RenameFails,        ///< o rename final falha
    BeforeWrite,        ///< deterministic interleaving at the snapshot/IO boundary
};

struct FaultInjection {
    Fault kind = Fault::None;
    u64 afterBytes = 0;
    /// Só caminhos que contêm este trecho (vazio = todos).
    std::string pathContains;
    /// Quantas escritas falham antes de a injeção se desligar sozinha.
    u32 count = 1;
    void (*beforeWrite)(void*) = nullptr;
    void* context = nullptr;
};

void set_fault_injection(const FaultInjection& fault) noexcept;
void clear_fault_injection() noexcept;
/// Escritas que falharam por injeção desde o último `set` (os testes conferem
/// que o caminho testado passou mesmo pela falha).
[[nodiscard]] u32 injected_failures() noexcept;

} // namespace aurea::fileio
