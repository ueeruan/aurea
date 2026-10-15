#include "TestFramework.hpp"
#include "aurea/render/PreRenderCache.hpp"
#include "aurea/export/ImageEncode.hpp"
#include <array>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

using namespace aurea;
#ifdef AUREA_PRERENDER_HEAP_PROBE
extern u64 prerender_heap_live();
extern u64 prerender_heap_peak();
extern void prerender_heap_reset();
#endif
namespace {
namespace fs = std::filesystem;
struct CacheFixture {
    fs::path directory;
    PreRenderCache cache;
    CacheFixture() {
        static std::atomic<u64> sequence{0};
        const auto root = fs::path(__FILE__).parent_path().parent_path().parent_path();
        directory = root / "build/validation/prerender-cache-20261013/fixtures" /
            ("test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++sequence));
        std::error_code error; fs::create_directories(directory, error); AUREA_CHECK(!error);
    }
    PreRenderCache::Key key(u32 w = 16, u32 h = 12) const { return {71, 99, 17, w, h, 30000, 1001, 1}; }
    Result<u64> begin(u64 count = 1, i32 direction = 1, PreRenderCache::Limits limits = {}, u32 w = 16, u32 h = 12) {
        return cache.begin(directory.string(), key(w, h), count, direction, limits);
    }
    fs::path owned() const {
        const auto parent = directory / "pre-render";
        for (const auto& child : fs::directory_iterator(parent)) if (child.is_directory()) return child.path();
        return {};
    }
};
std::vector<u16> pixels(u32 w, u32 h, u64 frame = 0) {
    std::vector<u16> result(usize(w) * h * 4);
    const u16 alpha[3]{0, 0x3800, 0x3c00}; // 0, 0.5, 1
    for (usize i = 0; i < result.size(); i += 4) {
        result[i + 3] = alpha[(i / 4 + frame) % 3];
        result[i] = result[i + 3] ? u16(0x4000 + (frame % 1024)) : 0; // HDR >= 2
        result[i + 1] = result[i + 3] ? 0x3400 : 0;
        result[i + 2] = result[i + 3] ? 0x3000 : 0;
    }
    return result;
}
Status load(PreRenderCache& cache, u64 token, u64 frame, std::vector<u16>& output) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        const Status queued = cache.request(token, frame);
        if (!queued.ok() && queued.code() != Errc::Busy) return queued;
        const Status result = cache.take(token, frame, output);
        if (result.code() != Errc::NotFound && result.code() != Errc::Busy) return result;
        if (std::chrono::steady_clock::now() >= deadline) return Errc::Timeout;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
Status request_available(PreRenderCache& cache, u64 token, u64 frame) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        const auto result = cache.request(token, frame);
        if (result.code() != Errc::Busy || std::chrono::steady_clock::now() >= deadline) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void flip(const fs::path& path, std::streamoff offset) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    AUREA_CHECK(file.good()); file.seekg(offset); char value = 0; file.read(&value, 1);
    AUREA_CHECK(file.good()); value ^= 0x40; file.seekp(offset); file.write(&value, 1); file.flush();
    AUREA_CHECK(file.good());
}
void ownership_marker(const fs::path& directory) {
    // Cache ownership is independent of a completed manifest: a crash may
    // happen before the first frame or while a .part file is being written.
    std::array<u8, 32> bytes{};
    std::memcpy(bytes.data(), "AURPRO01", 8); bytes[8] = 1;
    const u32 crc = crc32_update(0, bytes.data(), 28);
    for (u32 i = 0; i < 4; ++i) bytes[28 + i] = static_cast<u8>(crc >> (8 * i));
    std::ofstream file(directory / "owner.bin", std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
bool directory_link(const fs::path& link, const fs::path& target) {
    std::error_code ec; fs::create_directory_symlink(target, link, ec);
    if (!ec) return true;
#ifdef _WIN32
    // A junction exercises the same Windows reparse-point guard without
    // requiring Developer Mode's symbolic-link permission.
    fs::create_directory(link, ec); if (ec) return false;
    const auto handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const auto display = fs::absolute(target).native(), substitute = std::wstring(L"\\??\\") + display;
    struct Header { DWORD tag; USHORT length, reserved, subOffset, subLength, printOffset, printLength; };
    const auto subBytes = static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
    const auto printBytes = static_cast<USHORT>(display.size() * sizeof(wchar_t));
    const auto pathBytes = subBytes + printBytes + 4;
    std::vector<u8> buffer(sizeof(Header) + pathBytes);
    auto* header = reinterpret_cast<Header*>(buffer.data());
    *header = {IO_REPARSE_TAG_MOUNT_POINT, static_cast<USHORT>(8 + pathBytes), 0, 0, subBytes,
        static_cast<USHORT>(subBytes + 2), printBytes};
    std::memcpy(buffer.data() + sizeof(Header), substitute.c_str(), subBytes + 2);
    std::memcpy(buffer.data() + sizeof(Header) + subBytes + 2, display.c_str(), printBytes + 2);
    DWORD returned = 0;
    const auto ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(), static_cast<DWORD>(buffer.size()),
        nullptr, 0, &returned, nullptr);
    CloseHandle(handle); return ok != FALSE;
#else
    return false;
#endif
}
}

AUREA_TEST(PreRenderCache, OrphanCleanupPreservesTwoLiveSessionsAndUnknownFiles) {
    CacheFixture fixture;
    PreRenderCache second, third;
    const auto firstToken = fixture.begin(); AUREA_CHECK(firstToken.ok()); if (!firstToken.ok()) return;
    AUREA_CHECK(fixture.cache.write(*firstToken, 0, pixels(16, 12)).ok());
    const auto live = fixture.owned();
    const auto parent = live.parent_path();
    const auto orphan = parent / "owned-0000000000000001-0000000000000002";
    std::error_code error; fs::create_directory(orphan, error); AUREA_CHECK(!error);
    for (const auto& file : fs::directory_iterator(live)) if (file.path().filename() != "session.lease") {
        fs::copy_file(file.path(), orphan / file.path().filename(), error); AUREA_CHECK(!error);
    }
    ownership_marker(orphan); // Finite process-exit surrogate: no held lease.
    const auto unknown = parent / "owned-0000000000000003-0000000000000004";
    fs::create_directory(unknown); { std::ofstream file(unknown / "user-file"); file << "keep"; }
    const auto wrongVersion = parent / "owned-0000000000000005-0000000000000006";
    fs::copy(orphan, wrongVersion, fs::copy_options::recursive, error); AUREA_CHECK(!error);
    flip(wrongVersion / "owner.bin", 8);
    const auto secondToken = second.begin(fixture.directory.string(), fixture.key(), 1, 1, {});
    AUREA_CHECK(secondToken.ok()); if (!secondToken.ok()) return;
    AUREA_CHECK(!fs::exists(orphan)); AUREA_CHECK(fs::exists(live));
    AUREA_CHECK(fs::exists(unknown / "user-file")); AUREA_CHECK(fs::exists(wrongVersion));
    AUREA_CHECK(fixture.cache.matches(*firstToken, fixture.key()));
    const auto thirdToken = third.begin(fixture.directory.string(), fixture.key(), 1, 1, {});
    AUREA_CHECK(thirdToken.ok());
    AUREA_CHECK(second.matches(*secondToken, fixture.key())); AUREA_CHECK(fs::exists(live));
    AUREA_CHECK(fixture.cache.finish(*firstToken).ok());
}

AUREA_TEST(PreRenderCache, CumulativeDiskQuotaIncludesAnotherLiveInstance) {
    CacheFixture fixture; PreRenderCache other;
    PreRenderCache::Limits limits; limits.diskBytes = 512;
    const auto first = fixture.begin(1, 1, limits); AUREA_CHECK(first.ok()); if (!first.ok()) return;
    AUREA_CHECK(fixture.cache.write(*first, 0, pixels(16, 12)).ok());
    const auto second = other.begin(fixture.directory.string(), fixture.key(), 1, 1, limits);
    AUREA_CHECK(second.ok()); if (!second.ok()) return;
    AUREA_CHECK_EQ(other.write(*second, 0, pixels(16, 12)).code(), Errc::StorageFull);
    AUREA_CHECK(fixture.cache.matches(*first, fixture.key()));
    AUREA_CHECK(!other.stats().complete);
}

AUREA_TEST(PreRenderCache, OsLeaseAndSymlinksPreventDeletingHeldOrUnownedTargets) {
    CacheFixture fixture; PreRenderCache second, third;
    const auto first = fixture.begin(); AUREA_CHECK(first.ok()); if (!first.ok()) return;
    const auto parent = fixture.owned().parent_path();
    const auto orphan = parent / "owned-0000000000000011-0000000000000012";
    fs::create_directory(orphan); ownership_marker(orphan);
    { std::ofstream file(orphan / "frame-part.bin"); file << "partial cache"; }
#ifdef _WIN32
    const auto held = CreateFileW((orphan / "session.lease").c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    AUREA_CHECK(held != INVALID_HANDLE_VALUE); if (held == INVALID_HANDLE_VALUE) return;
#else
    const auto held = ::open((orphan / "session.lease").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    AUREA_CHECK(held >= 0); if (held < 0) return;
    AUREA_CHECK(::flock(held, LOCK_EX | LOCK_NB) == 0);
#endif
    const auto next = second.begin(fixture.directory.string(), fixture.key(), 1, 1, {});
    AUREA_CHECK(next.ok()); AUREA_CHECK(fs::exists(orphan / "frame-part.bin"));
#ifdef _WIN32
    CloseHandle(held);
#else
    ::close(held);
#endif
    const auto protectedDirectory = fixture.directory / "original-media";
    fs::create_directory(protectedDirectory); { std::ofstream file(protectedDirectory / "original.bin"); file << "original"; }
    const auto link = parent / "owned-0000000000000013-0000000000000014";
    const bool linksAvailable = directory_link(link, protectedDirectory);
    const auto linkedChild = parent / "owned-0000000000000015-0000000000000016";
    fs::create_directory(linkedChild); ownership_marker(linkedChild);
    if (linksAvailable) AUREA_CHECK(directory_link(linkedChild / "injected", protectedDirectory));
    else { fs::create_directory(linkedChild / "injected"); }
    const auto afterRelease = third.begin(fixture.directory.string(), fixture.key(), 1, 1, {});
    AUREA_CHECK(afterRelease.ok()); AUREA_CHECK(!fs::exists(orphan));
    AUREA_CHECK(fs::exists(protectedDirectory / "original.bin")); AUREA_CHECK(fs::exists(linkedChild));
    if (linksAvailable) {
#ifdef _WIN32
        AUREA_CHECK((GetFileAttributesW(link.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
#else
        AUREA_CHECK(fs::is_symlink(link));
#endif
    }
    std::printf("    real directory symlink guard %s; held OS lease released before orphan reclamation\n", linksAvailable ? "executed" : "unavailable (nested-directory guard executed)");
}

AUREA_TEST(PreRenderCache, FullProjectOver300FramesBothDirectionsPreservesAlphaHDRAndLogicalTimes) {
    for (const i32 direction : {1, -1}) {
        CacheFixture fixture;
        const auto token = fixture.begin(307, direction); AUREA_CHECK(token.ok()); if (!token.ok()) return;
        AUREA_CHECK(!fixture.cache.stats().complete);
        AUREA_CHECK_EQ(fixture.cache.finish(*token).code(), Errc::InvalidState);
        for (u64 done = 0; done < 307; ++done) {
            const u64 frame = direction > 0 ? done : 306 - done;
            const auto original = pixels(16, 12, frame);
            AUREA_CHECK(fixture.cache.write(*token, frame, original).ok());
        }
        AUREA_CHECK_EQ(fixture.cache.stats().framesWritten, u64{307});
        AUREA_CHECK(fixture.cache.finish(*token).ok()); AUREA_CHECK(fixture.cache.stats().complete);
        AUREA_CHECK(fixture.cache.finish(*token).ok());
        for (const u64 frame : {0ull, 1ull, 150ull, 300ull, 306ull}) {
            std::vector<u16> decoded; AUREA_CHECK(load(fixture.cache, *token, frame, decoded).ok());
            AUREA_CHECK(decoded == pixels(16, 12, frame));
        }
        AUREA_CHECK(fixture.cache.stats().peakOwnedBytes <= (16ull << 20));
        AUREA_CHECK(fs::exists(fixture.owned() / "complete.bin"));
    }
}

AUREA_TEST(PreRenderCache, ImmediateInvalidationRejectsOldTokenRevisionAndCleansOnlyPrivateDirectory) {
    CacheFixture fixture;
    const auto protectedFile = fixture.directory / "user-project.aurea";
    { std::ofstream file(protectedFile); file << "protected project"; }
    const auto token = fixture.begin(); AUREA_CHECK(token.ok()); if (!token.ok()) return;
    AUREA_CHECK(fixture.cache.write(*token, 0, pixels(16, 12)).ok()); AUREA_CHECK(fixture.cache.finish(*token).ok());
    const auto owned = fixture.owned();
    auto revised = fixture.key(); ++revised.modelRevision;
    AUREA_CHECK(fixture.cache.matches(*token, fixture.key())); AUREA_CHECK(!fixture.cache.matches(*token, revised));
    AUREA_CHECK(fixture.cache.request(*token, 0).ok());
    const auto begin = std::chrono::steady_clock::now(); fixture.cache.invalidate();
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::vector<u16> output;
    AUREA_CHECK_EQ(fixture.cache.take(*token, 0, output).code(), Errc::Cancelled);
    AUREA_CHECK_EQ(fixture.cache.request(*token, 0).code(), Errc::Cancelled);
    AUREA_CHECK_EQ(fixture.cache.finish(*token).code(), Errc::Cancelled);
    AUREA_CHECK(!fixture.cache.matches(*token, fixture.key())); AUREA_CHECK(!fixture.cache.stats().complete);
    AUREA_CHECK(elapsed < 20);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (fs::exists(owned) && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(!fs::exists(owned)); AUREA_CHECK(fs::exists(protectedFile));
    auto next = fixture.cache.begin(fixture.directory.string(), revised, 1, -1, {});
    while (next.code() == Errc::Busy && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        next = fixture.cache.begin(fixture.directory.string(), revised, 1, -1, {});
    }
    AUREA_CHECK(next.ok()); if (next.ok()) { AUREA_CHECK(*next != *token); AUREA_CHECK(fixture.cache.matches(*next, revised)); }
    std::printf("    invalidate %.3fms; unrelated project preserved\n", elapsed);
}

AUREA_TEST(PreRenderCache, CorruptFrameHeaderPayloadAndManifestNeverReturnPixelsOrComplete) {
    for (const u32 corruption : {0u, 1u, 2u}) {
        CacheFixture fixture; const auto token = fixture.begin(); AUREA_CHECK(token.ok()); if (!token.ok()) return;
        AUREA_CHECK(fixture.cache.write(*token, 0, pixels(16, 12)).ok()); AUREA_CHECK(fixture.cache.finish(*token).ok());
        if (corruption == 2) flip(fixture.owned() / "complete.bin", 24);
        else flip(fixture.owned() / "frame-00000000000000000000.bin", corruption ? 115 : 24);
        std::vector<u16> decoded; const Status result = load(fixture.cache, *token, 0, decoded);
        AUREA_CHECK(result.code() == Errc::CorruptData || result.code() == Errc::ChecksumMismatch);
        AUREA_CHECK(decoded.empty()); AUREA_CHECK(!fixture.cache.stats().complete);
        const auto diagnostic = fixture.cache.stats(); AUREA_CHECK(diagnostic.lastError != Errc::Ok);
        AUREA_CHECK(diagnostic.errorDetail[0] != '\0');
    }
}

AUREA_TEST(PreRenderCache, CancelledOrDiskLimitedJobsCannotPublishPartialCompleteCache) {
    {
        CacheFixture fixture; const auto token = fixture.begin(2); AUREA_CHECK(token.ok()); if (!token.ok()) return;
        std::atomic<bool> cancelled{true};
        AUREA_CHECK_EQ(fixture.cache.write(*token, 0, pixels(16, 12), &cancelled).code(), Errc::Cancelled);
        AUREA_CHECK_EQ(fixture.cache.stats().framesWritten, u64{0});
        AUREA_CHECK_EQ(fixture.cache.finish(*token).code(), Errc::Cancelled);
        AUREA_CHECK(!fs::exists(fixture.owned() / "complete.bin"));
    }
    {
        CacheFixture fixture; PreRenderCache::Limits limits; limits.diskBytes = 350;
        const auto token = fixture.begin(5, 1, limits); AUREA_CHECK(token.ok()); if (!token.ok()) return;
        Status result;
        for (u64 frame = 0; frame < 5 && result.ok(); ++frame) result = fixture.cache.write(*token, frame, pixels(16, 12, frame));
        AUREA_CHECK_EQ(result.code(), Errc::StorageFull); AUREA_CHECK(!fixture.cache.stats().complete);
        AUREA_CHECK_EQ(fixture.cache.finish(*token).code(), Errc::StorageFull);
        AUREA_CHECK(fixture.cache.stats().diskBytes <= limits.diskBytes);
        AUREA_CHECK(fixture.cache.stats().errorDetail[0] != '\0');
        for (const auto& file : fs::directory_iterator(fixture.owned())) AUREA_CHECK(file.path().extension() != ".part");
    }
    {
        CacheFixture fixture; PreRenderCache::Limits limits; limits.reserveFreeBytes = ~u64{0};
        AUREA_CHECK_EQ(fixture.begin(1, 1, limits).code(), Errc::StorageFull);
        AUREA_CHECK(!fixture.cache.stats().complete);
    }
}

// A janela de leitura cresce com o orçamento (antes: 2 quadros fixos, curto
// demais para tocar a 30 fps) e nunca passa dele nem de kMaxResidentFrames.
AUREA_TEST(PreRenderCache, ReadAheadWindowAndCompressionWorkspaceStayBoundedAt540p) {
    CacheFixture fixture; PreRenderCache::Limits limits; limits.residentFrames = 99;
    const auto token = fixture.begin(3, 1, limits, 960, 540); AUREA_CHECK(token.ok()); if (!token.ok()) return;
#ifdef AUREA_PRERENDER_HEAP_PROBE
    const u64 heapBaseline = prerender_heap_live(); prerender_heap_reset();
#endif
    auto original = pixels(960, 540);
    for (u64 frame = 0; frame < 3; ++frame) {
        if (frame == 2) {
            u32 noise = 0x13f725au;
            for (usize i = 0; i < original.size(); ++i) if (i % 4 != 3) {
                noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
                original[i] = static_cast<u16>(0x3000u | (noise & 0xfffu));
            }
        }
        AUREA_CHECK(fixture.cache.write(*token, frame, original).ok());
    }
    AUREA_CHECK(fixture.cache.finish(*token).ok());
    for (u32 iteration = 0; iteration < 200; ++iteration) {
        const auto queued = fixture.cache.request(*token, iteration % 3);
        AUREA_CHECK(queued.ok() || queued.code() == Errc::Busy);
        const auto stats = fixture.cache.stats();
        AUREA_CHECK(stats.residentLimit >= 2); AUREA_CHECK(stats.residentLimit <= PreRenderCache::kMaxResidentFrames);
        AUREA_CHECK(stats.residentFrames <= stats.residentLimit); AUREA_CHECK(stats.pendingReads <= stats.residentLimit);
        AUREA_CHECK(stats.residentBytes <= u64(960) * 540 * 8 * stats.residentLimit);
        AUREA_CHECK(stats.residentBytes + stats.workingBytes <= limits.memoryBytes);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::vector<u16> decoded; AUREA_CHECK(load(fixture.cache, *token, 2, decoded).ok()); AUREA_CHECK(decoded == original);
    const auto stats = fixture.cache.stats(); AUREA_CHECK(stats.peakOwnedBytes <= limits.memoryBytes);
    std::printf("    540p frame=%llu bytes; peak accounted workspace+resident=%llu bytes; reads=%u\n",
        static_cast<unsigned long long>(original.size() * sizeof(u16)),
        static_cast<unsigned long long>(stats.peakOwnedBytes), stats.residentFrames);
#ifdef AUREA_PRERENDER_HEAP_PROBE
    const u64 measured = prerender_heap_peak() - heapBaseline;
    AUREA_CHECK(measured <= limits.memoryBytes + original.capacity() * sizeof(u16) + (1ull << 20));
    std::printf("    measured native heap growth (cache+caller input/output included)=%llu bytes\n", static_cast<unsigned long long>(measured));
#endif
}

AUREA_TEST(PreRenderCache, InvalidationCancelsChunkedWriterBeforeItsDirectoryIsRetired) {
    CacheFixture fixture; const auto token = fixture.begin(1, 1, {}, 960, 540); AUREA_CHECK(token.ok()); if (!token.ok()) return;
    auto data = pixels(960, 540); u32 random = 1234567;
    for (auto& value : data) { random ^= random << 13; random ^= random >> 17; random ^= random << 5; value = static_cast<u16>(random); }
    std::atomic<bool> returned{false}; Status result;
    std::thread producer([&] { result = fixture.cache.write(*token, 0, data); returned.store(true); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!fixture.cache.stats().workingBytes && !returned.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    AUREA_CHECK(!returned.load());
    const auto began = std::chrono::steady_clock::now(); fixture.cache.invalidate();
    const f64 invalidationMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    AUREA_CHECK(invalidationMs < 20);
    producer.join(); AUREA_CHECK_EQ(result.code(), Errc::Cancelled);
    AUREA_CHECK(!fixture.cache.stats().complete);
}

// Medição: quanto custa ler um quadro 540p do disco (pedido → pronto). A
// reprodução a partir do cache só se sustenta se isto cabe num quadro de 30 fps.
AUREA_TEST(PreRenderCache, ReadLatencyAt540pIsMeasured) {
    CacheFixture fixture;
    const auto token = fixture.begin(4, 1, {}, 960, 540); AUREA_CHECK(token.ok()); if (!token.ok()) return;
    for (u64 frame = 0; frame < 4; ++frame) {
        auto data = pixels(960, 540, frame);
        if (frame >= 2) {
            u32 noise = 0x9e3779b9u + u32(frame);
            for (usize i = 0; i < data.size(); ++i) if (i % 4 != 3) {
                noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
                data[i] = static_cast<u16>(0x3000u | (noise & 0x3ffu));
            }
        }
        AUREA_CHECK(fixture.cache.write(*token, frame, data).ok());
    }
    AUREA_CHECK(fixture.cache.finish(*token).ok());
    for (u64 frame = 0; frame < 4; ++frame) {
        std::vector<u16> out;
        const auto t0 = std::chrono::steady_clock::now();
        AUREA_CHECK(load(fixture.cache, *token, frame, out).ok());
        const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("    540p read frame=%llu (%s) %.2f ms\n", static_cast<unsigned long long>(frame),
            frame >= 2 ? "noisy" : "flat", ms);
    }
}

AUREA_TEST(PreRenderCache, InvalidDimensionsOrderingAndMemoryBudgetAreExplicit) {
    CacheFixture fixture;
    AUREA_CHECK_EQ(fixture.begin(0).code(), Errc::InvalidArgument);
    AUREA_CHECK_EQ(fixture.begin(1, 0).code(), Errc::InvalidArgument);
    PreRenderCache::Limits limits; limits.memoryBytes = 1024;
    AUREA_CHECK_EQ(fixture.begin(1, 1, limits).code(), Errc::BudgetExceeded);
    const auto token = fixture.begin(2); AUREA_CHECK(token.ok()); if (!token.ok()) return;
    AUREA_CHECK_EQ(fixture.cache.write(*token, 1, pixels(16, 12)).code(), Errc::InvalidArgument);
    AUREA_CHECK_EQ(fixture.cache.write(*token, 0, std::span<const u16>{}).code(), Errc::InvalidArgument);
    AUREA_CHECK_EQ(request_available(fixture.cache, *token, 0).code(), Errc::NotFound);
    AUREA_CHECK_EQ(request_available(fixture.cache, *token, 2).code(), Errc::OutOfRange);
    AUREA_CHECK(!fixture.cache.stats().complete);
}

#ifdef _WIN32
AUREA_TEST(PreRenderCache, FailedCleanupRetainsDirectoryAndRejectsMoreDiskUntilLeaseIsReleased) {
    CacheFixture fixture; const auto token = fixture.begin(); AUREA_CHECK(token.ok()); if (!token.ok()) return;
    AUREA_CHECK(fixture.cache.write(*token, 0, pixels(16, 12)).ok()); AUREA_CHECK(fixture.cache.finish(*token).ok());
    const auto owned = fixture.owned();
    HANDLE locked = CreateFileW((owned / "complete.bin").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    AUREA_CHECK(locked != INVALID_HANDLE_VALUE); if (locked == INVALID_HANDLE_VALUE) return;
    fixture.cache.invalidate();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (fixture.cache.stats().lastError != Errc::IoError && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK_EQ(fixture.cache.stats().lastError, Errc::IoError);
    AUREA_CHECK(fixture.cache.stats().errorDetail[0] != '\0');
    AUREA_CHECK(fs::exists(owned));
    AUREA_CHECK(fs::exists(owned / "owner.bin"));
    AUREA_CHECK_EQ(fixture.begin().code(), Errc::IoError);
    AUREA_CHECK(!fixture.cache.stats().complete);
    CloseHandle(locked);
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (fs::exists(owned) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(!fs::exists(owned));
    auto next = fixture.begin();
    while (next.code() == Errc::Busy && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1)); next = fixture.begin();
    }
    AUREA_CHECK(next.ok());
}
#endif
