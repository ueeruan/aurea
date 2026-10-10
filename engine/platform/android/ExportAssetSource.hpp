#pragma once
#include "aurea/core/Result.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/project/FileIO.hpp"
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
namespace aurea::android_export {
// Gallery locators become durable recovery assets. Constant buffers, cancellable
// reads, finite deadlines, and exact byte comparison before reusing a prior copy.
inline Result<std::string> preserve_asset(const std::string& source, const std::string& documents,
    const std::atomic<bool>* cancel, int (*opener)(const char*, void*)) noexcept {
    if (source.rfind("content:", 0) != 0) return source;
    if (documents.empty() || !opener) return Status{Errc::NotSupported};
    const auto cancelled = [&] { return cancel && cancel->load(std::memory_order_acquire); };
    if (cancelled()) return Status{Errc::Cancelled};
    struct Fd { int value; ~Fd() { if (value >= 0) ::close(value); } } input{opener(source.c_str(), nullptr)};
    if (input.value < 0) return Status{Errc::MediaSourceMissing};
    const int flags = ::fcntl(input.value, F_GETFL);
    if (flags >= 0) (void)::fcntl(input.value, F_SETFL, flags | O_NONBLOCK);
    std::error_code error;
    const auto root = std::filesystem::path(documents) / "export-sources";
    std::filesystem::create_directories(root, error);
    if (error) return Status{Errc::IoError};
    struct stat before{}; const bool sized = ::fstat(input.value, &before) == 0 && S_ISREG(before.st_mode);
    struct statvfs space{};
    if (sized && before.st_size > 0 && ::statvfs(root.c_str(), &space) == 0) {
        const u64 available = static_cast<u64>(space.f_bavail) * space.f_frsize;
        if (available < static_cast<u64>(before.st_size) || available - before.st_size < (8ull << 20)) return Status{Errc::StorageFull};
    }
    static std::atomic<u64> sequence{0};
    const std::string temporary = (root / ("copy-" + std::to_string(monotonic_ns()) + "-" + std::to_string(++sequence) + ".tmp")).string();
    struct Remove { const std::string& path; ~Remove() { ::unlink(path.c_str()); } } remove{temporary};
    Fd output{::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600)};
    if (output.value < 0) return Status{errno == ENOSPC ? Errc::StorageFull : Errc::IoError};
    std::array<u8, 65536> buffer{}; u64 hash = 1469598103934665603ull, bytes = 0;
    const u64 started = monotonic_ns(); u64 progress = started;
    for (;;) {
        if (cancelled()) return Status{Errc::Cancelled};
        const u64 now = monotonic_ns();
        if (now - progress > 30'000'000'000ull || now - started > 120'000'000'000ull) return Status{Errc::Timeout};
        const ssize_t count = ::read(input.value, buffer.data(), buffer.size());
        if (count < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) { pollfd p{input.value, POLLIN, 0}; (void)::poll(&p, 1, 100); continue; }
            return Status{Errc::IoError};
        }
        if (!count) break;
        for (ssize_t offset = 0; offset < count;) {
            if (cancelled()) return Status{Errc::Cancelled};
            const auto wrote = ::write(output.value, buffer.data() + offset, static_cast<size_t>(count - offset));
            if (wrote < 0 && errno == EINTR) continue;
            if (wrote <= 0) return Status{errno == ENOSPC ? Errc::StorageFull : Errc::IoError};
            offset += wrote;
        }
        for (ssize_t i = 0; i < count; ++i) { hash ^= buffer[i]; hash *= 1099511628211ull; }
        bytes += static_cast<u64>(count); progress = monotonic_ns();
    }
    if (!bytes || (sized && before.st_size > 0 && bytes != static_cast<u64>(before.st_size))) return Status{Errc::DecodeFailed};
    struct stat after{};
    if (sized && (::fstat(input.value, &after) != 0 || after.st_size != before.st_size || after.st_mtim.tv_sec != before.st_mtim.tv_sec || after.st_mtim.tv_nsec != before.st_mtim.tv_nsec)) return Status{Errc::InvalidState};
    if (::fsync(output.value) != 0) return Status{errno == ENOSPC ? Errc::StorageFull : Errc::IoError};
    ::close(output.value); output.value = -1;
    char name[96]{}; std::snprintf(name, sizeof(name), "%016llx-%llu.media", static_cast<unsigned long long>(hash), static_cast<unsigned long long>(bytes));
    std::string final = (root / name).string();
    if (fileio::exists(final)) {
        Fd prior{::open(final.c_str(), O_RDONLY | O_CLOEXEC)}, copy{::open(temporary.c_str(), O_RDONLY | O_CLOEXEC)};
        std::array<u8, 65536> other{}; bool same = prior.value >= 0 && copy.value >= 0;
        for (u64 compared = 0; same && compared < bytes;) {
            if (cancelled()) return Status{Errc::Cancelled};
            if (monotonic_ns() - started > 120'000'000'000ull) return Status{Errc::Timeout};
            const auto a = ::read(prior.value, buffer.data(), buffer.size()), b = ::read(copy.value, other.data(), other.size());
            same = a > 0 && a == b && std::memcmp(buffer.data(), other.data(), static_cast<size_t>(a)) == 0;
            if (same) compared += static_cast<u64>(a);
        }
        struct stat existing{}; same = same && ::fstat(prior.value, &existing) == 0 && existing.st_size == static_cast<off_t>(bytes);
        if (same) return final;
        final += "." + std::to_string(monotonic_ns()) + ".media";
    }
    if (cancelled()) return Status{Errc::Cancelled};
    if (const Status committed = fileio::commit_file(temporary, final); !committed.ok()) return committed;
    AUREA_LOG_INFO("export-v2 phase=preparing persistent_source_bytes=%llu duration_ns=%llu", static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(monotonic_ns() - started));
    return final;
}
} // namespace aurea::android_export
