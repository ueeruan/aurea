// =============================================================================
//  Aurea / project / FileIO.cpp — ver FileIO.hpp.
// =============================================================================
#include "aurea/project/FileIO.hpp"
#include "aurea/core/Log.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <io.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace aurea::fileio {
namespace {

// --- Injeção de falha ----------------------------------------------------------
std::mutex        g_faultMutex;
FaultInjection    g_fault;
std::atomic<bool> g_faultArmed{false};
std::atomic<u32>  g_injected{0};

/// A falha `kind` vale para `path` agora? Consome uma ocorrência.
bool take_fault(Fault kind, const std::string& path, u64* afterBytes = nullptr) noexcept {
    if (!g_faultArmed.load(std::memory_order_relaxed)) return false;
    std::lock_guard<std::mutex> lock(g_faultMutex);
    if (g_fault.kind != kind || g_fault.count == 0) return false;
    if (!g_fault.pathContains.empty() && path.find(g_fault.pathContains) == std::string::npos) return false;
    if (afterBytes) *afterBytes = g_fault.afterBytes;
    if (--g_fault.count == 0) g_faultArmed.store(false, std::memory_order_relaxed);
    g_injected.fetch_add(1, std::memory_order_relaxed);
    return true;
}

/// Uma escrita atômica por vez no processo: o temporário tem nome fixo
/// (`<path>.tmp`, é o que o leitor procura depois de uma queda) e duas
/// gravações simultâneas do mesmo projeto — autosave + "Salvar" + ir para
/// segundo plano — escreveriam intercaladas no mesmo temporário.
std::mutex g_writeMutex;

bool is_full(int err) noexcept {
#if defined(EDQUOT)
    if (err == EDQUOT) return true;
#endif
    return err == ENOSPC;
}

Status fail(int err, const char* what, std::string* outError) noexcept {
    if (outError) {
        *outError = what;
        if (err) { *outError += ": "; *outError += std::strerror(err); }
    }
    return is_full(err) ? Status{Errc::StorageFull, "armazenamento cheio"} : Status{Errc::IoError, what};
}

int sync_file(std::FILE* f) noexcept {
#if defined(_WIN32)
    return _commit(_fileno(f));
#else
    return ::fsync(fileno(f));
#endif
}

/// Escreve o temporário por completo. 0 = ok; senão o errno da falha.
int write_temp(const std::string& tmp, const void* data, usize size, bool doSync) noexcept {
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (f && take_fault(Fault::OpenFails, tmp)) {
        std::fclose(f);
        std::remove(tmp.c_str());
        f = nullptr;
        errno = EACCES;
    }
    if (!f) return errno ? errno : EIO;

    usize limit = size;
    bool injectedFull = false;
    u64 after = 0;
    if (take_fault(Fault::DiskFullAfter, tmp, &after) && after < size) {
        limit = static_cast<usize>(after);
        injectedFull = true;
    }
    int err = 0;
    errno = 0;
    const usize written = limit ? std::fwrite(data, 1, limit, f) : 0;
    if (written != limit) err = errno ? errno : EIO;
    else if (injectedFull) err = ENOSPC;
    // Disco cheio costuma aparecer AQUI: o fwrite só encheu o buffer da libc.
    if (!err && std::fflush(f) != 0) err = errno ? errno : EIO;
    if (!err && take_fault(Fault::FlushFails, tmp)) err = ENOSPC;
    if (!err && doSync && sync_file(f) != 0) err = errno ? errno : EIO;
    if (std::fclose(f) != 0 && !err) err = errno ? errno : EIO;
    if (err) std::remove(tmp.c_str());
    return err;
}

#if defined(_WIN32)
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<usize>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
#else
void sync_parent_dir(const std::string& path) noexcept {
    // O rename só é durável depois que a pasta foi para o disco. Melhor
    // esforço: uma pasta que não abre não invalida a gravação já feita.
    const usize slash = path.find_last_of('/');
    const std::string dir = slash == std::string::npos ? std::string(".") : path.substr(0, slash ? slash : 1);
    const int fd = ::open(dir.c_str(), O_RDONLY);
    if (fd >= 0) {
        (void)::fsync(fd);
        ::close(fd);
    }
}

/// Cópia simples (usada quando o hard link do .bak é recusado).
bool copy_plain(const std::string& from, const std::string& to) noexcept {
    std::vector<u8> bytes;
    if (!read_all(from, bytes, 2ull * 1024 * 1024 * 1024)) return false;
    return write_temp(to, bytes.data(), bytes.size(), true) == 0;
}
#endif

/// Troca o principal pelo temporário. Com `keepBackup`, o principal antigo
/// vira `.bak` SEM janela em que o principal não existe.
bool replace_with(const std::string& tmp, const std::string& path, bool keepBackup, bool doSync) noexcept {
    const bool hadMain = exists(path);
#if defined(_WIN32)
    (void)doSync;
    const std::wstring wt = widen(tmp), wp = widen(path);
    if (keepBackup && hadMain) {
        const std::wstring wb = widen(backup_path(path));
        DeleteFileW(wb.c_str());
        if (ReplaceFileW(wp.c_str(), wt.c_str(), wb.c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            return true;
        }
        AUREA_LOG_WARN("ReplaceFileW falhou (%lu): gravando sem copia de seguranca", GetLastError());
    }
    return MoveFileExW(wt.c_str(), wp.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (keepBackup && hadMain) {
        const std::string bak = backup_path(path);
        ::unlink(bak.c_str());
        if (::link(path.c_str(), bak.c_str()) != 0) {
            // Sistema de arquivos (ou política SELinux) sem hard link: copia.
            const std::string bakTmp = bak + ".tmp";
            if (copy_plain(path, bakTmp)) {
                if (std::rename(bakTmp.c_str(), bak.c_str()) != 0) std::remove(bakTmp.c_str());
            } else {
                AUREA_LOG_WARN("copia de seguranca nao gravada (%s): seguindo sem .bak", std::strerror(errno));
            }
        }
    }
    // POSIX: rename substitui atomicamente. Sem o antigo "apaga o destino e
    // tenta de novo": se o rename falhou, apagar o destino perderia o projeto.
    if (std::rename(tmp.c_str(), path.c_str()) != 0) return false;
    if (doSync) sync_parent_dir(path);
    return true;
#endif
}

} // namespace

bool exists(const std::string& path) noexcept {
    if (path.empty()) return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

bool read_all(const std::string& path, std::vector<u8>& out, usize maxBytes) noexcept {
    out.clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
    const long size = std::ftell(f);
    if (size < 0 || static_cast<unsigned long>(size) > maxBytes || std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<usize>(size));
    const usize got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (got != out.size()) { out.clear(); return false; }
    return true;
}

Status write_atomic(const std::string& path, const void* data, usize size,
                    const AtomicWriteOptions& options, std::string* outError) noexcept {
    if (path.empty() || (!data && size)) return Status{Errc::InvalidArgument, "caminho ou dados invalidos"};
    std::lock_guard<std::mutex> lock(g_writeMutex);

    const std::string tmp = temp_path(path);
    if (const int err = write_temp(tmp, data, size, options.fsync); err != 0) {
        AUREA_LOG_ERROR("gravacao atomica: temporario falhou (%s); o arquivo anterior segue intacto",
                        std::strerror(err));
        return fail(err, "nao foi possivel gravar o temporario", outError);
    }
    if (take_fault(Fault::RenameFails, path) || !replace_with(tmp, path, options.keepBackup, options.fsync)) {
        const int err = errno;
        std::remove(tmp.c_str());
        AUREA_LOG_ERROR("gravacao atomica: rename falhou; o arquivo anterior segue intacto");
        return fail(err == ENOSPC ? err : 0, "nao foi possivel substituir o arquivo", outError);
    }
    return OkStatus;
}

Status copy_file(const std::string& from, const std::string& to) noexcept {
    std::vector<u8> bytes;
    if (!read_all(from, bytes, 2ull * 1024 * 1024 * 1024)) return Status{Errc::IoError, "origem ilegivel"};
    return write_atomic(to, bytes.data(), bytes.size(), AtomicWriteOptions{});
}

void set_fault_injection(const FaultInjection& fault) noexcept {
    std::lock_guard<std::mutex> lock(g_faultMutex);
    g_fault = fault;
    g_injected.store(0, std::memory_order_relaxed);
    g_faultArmed.store(fault.kind != Fault::None && fault.count > 0, std::memory_order_relaxed);
}

void clear_fault_injection() noexcept {
    std::lock_guard<std::mutex> lock(g_faultMutex);
    g_fault = FaultInjection{};
    g_faultArmed.store(false, std::memory_order_relaxed);
}

u32 injected_failures() noexcept { return g_injected.load(std::memory_order_relaxed); }

} // namespace aurea::fileio
