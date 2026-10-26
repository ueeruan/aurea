// =============================================================================
//  Aurea / hyper / neural / NeuralCommon.cpp — ver NeuralCommon.hpp.
// =============================================================================
#include "hyper/neural/NeuralCommon.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/platform/AddressSpace.hpp"

#include <cpu.h>
#include <datareader.h>
#include <gpu.h>
#include <layer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <pthread.h>
#include <sys/qos.h>
#include <sys/sysctl.h>
#if TARGET_OS_IPHONE
#include <os/proc.h>
#endif
#endif

bool aurea_hyper_embedded_lookup(const char* id, const unsigned char** data, std::size_t* size);   // gerado

namespace aurea::hyper::neural {

ncnn::Layer* rife_warp_creator(void* userdata);   // NeuralMotion.cpp

namespace {
std::mutex g_modelDirMutex;
std::string g_modelDir;
} // namespace
} // namespace aurea::hyper::neural

namespace aurea::hyper {
void set_model_directory(const std::string& path) {
    std::lock_guard<std::mutex> lock(neural::g_modelDirMutex);
    neural::g_modelDir = path;
}
std::string model_directory() {
    std::lock_guard<std::mutex> lock(neural::g_modelDirMutex);
    return neural::g_modelDir;
}
} // namespace aurea::hyper

namespace aurea::hyper::neural {

// --- SHA-256 (FIPS 180-4) --------------------------------------------------------
namespace {
struct Sha256 {
    u32 h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    u8 block[64]{};
    u64 total = 0;
    u32 fill = 0;
    static u32 rotr(u32 x, u32 n) { return (x >> n) | (x << (32 - n)); }
    void compress(const u8* p) {
        static constexpr u32 k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        u32 w[64];
        for (u32 i = 0; i < 16; ++i)
            w[i] = (u32(p[i * 4]) << 24) | (u32(p[i * 4 + 1]) << 16) | (u32(p[i * 4 + 2]) << 8) | u32(p[i * 4 + 3]);
        for (u32 i = 16; i < 64; ++i) {
            const u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const u32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (u32 i = 0; i < 64; ++i) {
            const u32 t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const u32 t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const u8* p, usize n) {
        total += n;
        while (n) {
            if (fill == 0 && n >= 64) { compress(p); p += 64; n -= 64; continue; }
            const u32 take = static_cast<u32>(std::min<usize>(64 - fill, n));
            std::memcpy(block + fill, p, take);
            fill += take; p += take; n -= take;
            if (fill == 64) { compress(block); fill = 0; }
        }
    }
    std::string hex() {
        const u64 bits = total * 8;
        const u8 one = 0x80, zero = 0;
        update(&one, 1);
        while (fill != 56) update(&zero, 1);
        u8 len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<u8>(bits >> (56 - 8 * i));
        update(len, 8);
        static const char* digits = "0123456789abcdef";
        std::string s;
        s.reserve(64);
        for (u32 v : h)
            for (int i = 28; i >= 0; i -= 4) s.push_back(digits[(v >> i) & 15]);
        return s;
    }
};

struct OndemandFile { const char* id; const char* file; const char* sha256; u64 bytes; };
// Os hashes do manifesto de cada modelo sob demanda (tools/hyper_models.py).
constexpr OndemandFile kOndemand[] = {
#include "hyper/neural/OndemandHashes.inl"
};
std::mutex g_verifiedMutex;
std::vector<std::string> g_verified;
} // namespace

Blob embedded(const char* id) noexcept try {
    const unsigned char* data = nullptr;
    std::size_t size = 0;
    if (!id || !aurea_hyper_embedded_lookup(id, &data, &size)) return {};
    return {data, size};
} catch (...) {
    return {};
}

std::string sha256_hex(const unsigned char* data, usize size) {
    Sha256 s;
    s.update(data, size);
    return s.hex();
}

Status verify_ondemand(const char* id, const char* file, std::string& path) {
    const OndemandFile* entry = nullptr;
    for (const auto& e : kOndemand)
        if (std::strcmp(e.id, id) == 0 && std::strcmp(e.file, file) == 0) entry = &e;
    if (!entry) return Status{Errc::NotFound, "modelo sob demanda desconhecido"};
    const std::string dir = model_directory();
    if (dir.empty()) return Status{Errc::NotFound, "pasta de modelos sob demanda nao definida"};
    path = dir + "/" + id + "/" + file;
    {
        std::lock_guard<std::mutex> lock(g_verifiedMutex);
        if (std::find(g_verified.begin(), g_verified.end(), path) != g_verified.end()) return OkStatus;
    }
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return Status{Errc::NotFound, "modelo sob demanda ausente"};
    Sha256 sha;
    std::vector<u8> chunk(1u << 20);
    u64 total = 0;
    for (;;) {
        const usize got = std::fread(chunk.data(), 1, chunk.size(), f);
        if (!got) break;
        sha.update(chunk.data(), got);
        total += got;
    }
    std::fclose(f);
    if (total != entry->bytes) return Status{Errc::ChecksumMismatch, "modelo sob demanda com tamanho diferente do manifesto"};
    if (sha.hex() != entry->sha256) return Status{Errc::ChecksumMismatch, "SHA-256 do modelo sob demanda difere do manifesto"};
    std::lock_guard<std::mutex> lock(g_verifiedMutex);
    g_verified.push_back(path);
    return OkStatus;
}

Status load_ondemand(const char* id, const char* file, OwnedBlob& out) {
    std::string path;
    const Status v = verify_ondemand(id, file, path);
    if (!v.ok()) return v;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return Status{Errc::NotFound, "modelo sob demanda ausente"};
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) { std::fclose(f); return Errc::IoError; }
    out.bytes.resize(static_cast<usize>(size) + 1);
    const usize got = std::fread(out.bytes.data(), 1, static_cast<usize>(size), f);
    std::fclose(f);
    out.bytes[static_cast<usize>(size)] = 0;
    if (got != static_cast<usize>(size)) { out.bytes.clear(); return Status{Errc::IoError, "leitura do modelo sob demanda"}; }
    return OkStatus;
}

} // namespace aurea::hyper::neural

namespace aurea::hyper {
bool ondemand_model_ready(const char* id) noexcept try {
    const std::string dir = model_directory();
    if (dir.empty() || !id) return false;
    bool any = false;
    for (const auto& e : neural::kOndemand) {
        if (std::strcmp(e.id, id) != 0) continue;
        any = true;
        const std::string path = dir + "/" + id + "/" + e.file;
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fclose(f);
        if (size <= 0 || static_cast<u64>(size) != e.bytes) return false;
    }
    return any;
} catch (...) {
    return false;
}
} // namespace aurea::hyper

namespace aurea::hyper::neural {

// --- Memória -------------------------------------------------------------------------
u64 system_available_bytes() noexcept {
#if defined(_WIN32)
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) return ms.ullAvailPhys;
    return 0;
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE
    if (__builtin_available(iOS 13.0, *)) return static_cast<u64>(os_proc_available_memory());
    return 0;
#else
    u64 mem = 0;
    size_t len = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0) return mem / 2;
    return 0;
#endif
#else
    std::FILE* f = std::fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[160];
    u64 kb = 0;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "MemAvailable:", 13) == 0) { kb = std::strtoull(line + 13, nullptr, 10); break; }
    }
    std::fclose(f);
    return kb * 1024ull;
#endif
}

u64 address_space_headroom() noexcept {
    if constexpr (!kAddressSpace32) {
        return ~0ull >> 2;
    } else {
#if defined(_WIN32)
        for (u64 size = 1536ull << 20; size >= (8ull << 20); size /= 2) {
            void* p = VirtualAlloc(nullptr, static_cast<SIZE_T>(size), MEM_RESERVE, PAGE_NOACCESS);
            if (p) { VirtualFree(p, 0, MEM_RELEASE); return size; }
        }
        return 0;
#else
        // Reserva sem acesso e sem memória (só endereços), devolvida na hora.
        u64 lo = 0;
        for (u64 size = 1536ull << 20; size >= (8ull << 20); size /= 2) {
            void* p = mmap(nullptr, static_cast<size_t>(size), PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (p != MAP_FAILED) { munmap(p, static_cast<size_t>(size)); lo = size; break; }
        }
        if (!lo) return 0;
        // Refina entre lo e 2·lo (4 passos).
        u64 hi = lo * 2;
        for (int i = 0; i < 4; ++i) {
            const u64 mid = (lo + hi) / 2 & ~0xFFFFFull;
            void* p = mmap(nullptr, static_cast<size_t>(mid), PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (p != MAP_FAILED) { munmap(p, static_cast<size_t>(mid)); lo = mid; } else { hi = mid; }
        }
        return lo;
#endif
    }
}

u64 inference_budget(const DeviceProfile& profile, u64 fixedBytes) noexcept {
    u64 avail = system_available_bytes();
    if (profile.freeRamBytes) avail = avail ? std::min(avail, profile.freeRamBytes) : profile.freeRamBytes;
    if (!avail) avail = 512ull << 20;   // sem informação: o mínimo plausível de um aparelho suportado
    // Um quarto do que o sistema diz estar livre: o resto é do decoder, do
    // encoder, da GPU e da interface (o export roda com tudo isso vivo).
    u64 budget = avail / 4;
    if constexpr (kAddressSpace32) {
        const u64 headroom = address_space_headroom();
        budget = std::min(budget, headroom / 3);
        budget = std::min<u64>(budget, 192ull << 20);
    } else {
        budget = std::min<u64>(budget, 1ull << 30);
    }
    return budget > fixedBytes ? budget - fixedBytes : 0;
}

u32 cpu_workers(const DeviceProfile& profile) noexcept {
    int big = ncnn::get_big_cpu_count();
    if (big <= 0) big = ncnn::get_cpu_count();
    const u32 cap = (profile.abi64 && !kAddressSpace32) ? 4u : 2u;
    return std::clamp<u32>(static_cast<u32>(std::max(big, 1)), 1u, cap);
}

void pin_to_big_cores() noexcept {
#if defined(__ANDROID__)
    // Sem OpenMP no ncnn, set_cpu_thread_affinity prende a thread atual.
    ncnn::set_cpu_thread_affinity(ncnn::get_cpu_thread_affinity_mask(2));
#elif defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif
}

void* BoundedAllocator::fastMalloc(size_t size) {
    constexpr size_t kHeader = 64;
    const u64 need = static_cast<u64>(size) + kHeader;
    u64 live = live_.fetch_add(need, std::memory_order_relaxed) + need;
    if (live > budget_) {
        live_.fetch_sub(need, std::memory_order_relaxed);
        exceeded_.store(true, std::memory_order_relaxed);
        return nullptr;
    }
    auto* base = static_cast<unsigned char*>(ncnn::fastMalloc(size + kHeader));
    if (!base) {
        live_.fetch_sub(need, std::memory_order_relaxed);
        return nullptr;
    }
    *reinterpret_cast<size_t*>(base) = size + kHeader;
    u64 peak = peak_.load(std::memory_order_relaxed);
    while (live > peak && !peak_.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {}
    return base + kHeader;
}

void BoundedAllocator::fastFree(void* ptr) {
    if (!ptr) return;
    auto* base = static_cast<unsigned char*>(ptr) - 64;
    live_.fetch_sub(*reinterpret_cast<size_t*>(base), std::memory_order_relaxed);
    ncnn::fastFree(base);
}

// --- Workers -------------------------------------------------------------------------
Status WorkerPool::start(u32 workers) noexcept try {
    stop();
    stopping_ = false;
    for (u32 i = 1; i < std::max(workers, 1u); ++i) threads_.emplace_back([this, i] { loop(i); });
    return OkStatus;
} catch (...) {
    stop();
    return Status{Errc::OutOfMemory, "threads das redes"};
}

void WorkerPool::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        ++generation_;
    }
    wake_.notify_all();
    for (auto& t : threads_) if (t.joinable()) t.join();
    threads_.clear();
}

void WorkerPool::loop(u32 worker) noexcept {
    pin_to_big_cores();
    u64 seen = 0;
    for (;;) {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [&] { return stopping_ || generation_ != seen; });
        if (stopping_) return;
        seen = generation_;
        while (fn_ && next_ < jobs_) {
            const u32 job = next_++;
            ++busy_;
            lock.unlock();
            (*fn_)(job, worker);
            lock.lock();
            --busy_;
        }
        if (busy_ == 0) done_.notify_all();
    }
}

void WorkerPool::run(u32 jobs, const std::function<void(u32, u32)>& fn) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fn_ = &fn;
        jobs_ = jobs;
        next_ = 0;
        busy_ = 0;
        ++generation_;
    }
    wake_.notify_all();
    std::unique_lock<std::mutex> lock(mutex_);
    while (next_ < jobs_) {
        const u32 job = next_++;
        ++busy_;
        lock.unlock();
        fn(job, 0);
        lock.lock();
        --busy_;
    }
    done_.wait(lock, [&] { return busy_ == 0 && next_ >= jobs_; });
    fn_ = nullptr;
}

// --- Backend -------------------------------------------------------------------------
bool vulkan_device_available() noexcept {
#if NCNN_VULKAN
    if (ncnn::get_gpu_count() <= 0) return false;
    const int index = ncnn::get_default_gpu_index();
    const ncnn::VulkanDevice* device = index >= 0 ? ncnn::get_gpu_device(index) : nullptr;
    return device && device->is_valid() && ncnn::get_gpu_info(index).type() != 3;
#else
    return false;
#endif
}

void NetRunner::release() noexcept {
    net_.clear();
    loaded_ = false;
}

Status NetRunner::load(const NetSource& source, Backend backend, bool fp16Arithmetic, bool allowFallback) noexcept try {
    release();
    source_ = source;
    fallback_ = allowFallback;
    fp16_ = fp16Arithmetic;
    backend_ = Backend::Cpu;
    if (!source.param || (!source.weights && source.weightsFile.empty()))
        return Status{Errc::NotFound, "modelo da rede ausente neste build"};
    net_.opt = ncnn::Option();
    net_.opt.lightmode = source.lightmode;
    net_.opt.num_threads = 1;
    net_.opt.use_vulkan_compute = false;
#if NCNN_VULKAN
    if (backend == Backend::Vulkan && vulkan_device_available()) {
        net_.set_vulkan_device(ncnn::get_default_gpu_index());
        net_.opt.use_vulkan_compute = true;
        backend_ = Backend::Vulkan;
    }
#endif
    if (backend == Backend::Vulkan && backend_ != Backend::Vulkan && !allowFallback)
        return Status{Errc::NotSupported, "Vulkan indisponivel para a rede"};
    const bool vulkan = backend_ == Backend::Vulkan;
    // GPU: fp16 no armazenamento e no empacotamento (banda); a conta em fp16
    // só quando o medidor conferiu o resultado neste driver.
    net_.opt.use_fp16_packed = vulkan;
    net_.opt.use_fp16_storage = vulkan;
    net_.opt.use_fp16_arithmetic = vulkan && fp16Arithmetic;
    net_.opt.use_fp16_uniform = vulkan && fp16Arithmetic;
    net_.opt.use_bf16_storage = false;
    if (!vulkan && source.cpuFp16) {
        net_.opt.use_fp16_storage = true;   // o ncnn só usa onde a CPU tem fp16
        net_.opt.use_fp16_packed = true;
        net_.opt.use_fp16_arithmetic = source.cpuFp16Arithmetic;
    }
    // CPU: fp32 na conta (o mesmo resultado em todo aparelho; int8 vem do
    // próprio .param quantizado), Winograd nas 3×3.
    net_.opt.use_winograd_convolution = !vulkan;
    net_.opt.use_sgemm_convolution = true;
    net_.opt.use_int8_inference = true;
    net_.opt.use_shader_local_memory = false;   // DEVICE_LOST visto no ncnn fixado (DepthEstimator)
    // O registro de camadas do ncnn sobrevive ao Net::clear(): uma vez por rede.
    if (source.rifeWarp && !warpRegistered_) { net_.register_custom_layer("rife.Warp", rife_warp_creator); warpRegistered_ = true; }
    int result = net_.load_param_mem(source.param.text());
    if (result == 0 && vulkan && source.cpuOnlyLayer) {
        for (auto* layer : net_.mutable_layers())
            if (layer && layer->name == source.cpuOnlyLayer) layer->support_vulkan = false;
    }
    if (result == 0 && !source.weightsFile.empty()) {
        std::FILE* f = std::fopen(source.weightsFile.c_str(), "rb");
        if (!f) {
            result = -1;
        } else {
            ncnn::DataReaderFromStdio reader(f);
            result = net_.load_model(reader);
            std::fclose(f);
        }
    } else if (result == 0) {
        const unsigned char* cursor = source.weights.data;
        ncnn::DataReaderFromMemory reader(cursor);
        result = net_.load_model(reader);
    }
    if (result != 0) {
        net_.clear();
        if (vulkan && allowFallback) return load(source, Backend::Cpu, false, false);
        return Status{Errc::CorruptData, "a rede nao carregou"};
    }
    loaded_ = true;
    return OkStatus;
} catch (const std::bad_alloc&) {
    net_.clear();
    loaded_ = false;
    return Errc::OutOfMemory;
}

Status NetRunner::fallback_to_cpu() noexcept {
    if (backend_ != Backend::Vulkan) return Errc::InvalidState;
    AUREA_LOG_WARN("hyper: tile falhou no Vulkan; seguindo na CPU");
    return load(source_, Backend::Cpu, false, false);
}

bool NetRunner::infer(const ncnn::Mat& in, ncnn::Mat& out, ncnn::Allocator* allocator) noexcept try {
    if (!loaded_) return false;
    ncnn::Extractor ex = net_.create_extractor();
    if (allocator) {
        ex.set_blob_allocator(allocator);
        ex.set_workspace_allocator(allocator);
    }
    return ex.input(source_.input, in) == 0 && ex.extract(source_.output, out) == 0 && !out.empty();
} catch (...) {
    return false;
}

// --- Conversões --------------------------------------------------------------------------
void mat_to_rgb(const ncnn::Mat& m, u32 div, u32 sx, u32 sy, u32 w, u32 h, u8* dst, u32 dstStride) noexcept {
    const f32 scale = 255.0f / static_cast<f32>(div * div);
    for (u32 c = 0; c < 3; ++c) {
        const float* plane = m.channel(static_cast<int>(c));
        for (u32 y = 0; y < h; ++y) {
            u8* row = dst + static_cast<usize>(y) * dstStride + c;
            for (u32 x = 0; x < w; ++x) {
                f32 acc = 0.0f;
                for (u32 dy = 0; dy < div; ++dy) {
                    const float* src = plane + static_cast<usize>((sy + y) * div + dy) * m.w + (sx + x) * div;
                    for (u32 dx = 0; dx < div; ++dx) acc += src[dx];
                }
                const f32 v = acc * scale + 0.5f;
                row[x * 3] = static_cast<u8>(v <= 0.0f ? 0 : v >= 255.0f ? 255 : static_cast<int>(v));
            }
        }
    }
}

void reference_pattern(u32 w, u32 h, u8* rgb) noexcept {
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x)
            for (u32 c = 0; c < 3; ++c) {
                const u64 v = 48 + ((static_cast<u64>(x) * x + 2ull * y * y + 500ull * c) / 24) % 128
                            + 24 * (((x / 8) + (y / 8) + c) % 2);
                rgb[(static_cast<usize>(y) * w + x) * 3 + c] = static_cast<u8>(v);
            }
}

f64 psnr_rgb(const u8* a, const u8* b, usize bytes) noexcept {
    if (!bytes) return 0.0;
    f64 se = 0.0;
    for (usize i = 0; i < bytes; ++i) {
        const f64 d = static_cast<f64>(a[i]) - static_cast<f64>(b[i]);
        se += d * d;
    }
    if (se == 0.0) return 99.0;
    return 10.0 * std::log10(255.0 * 255.0 / (se / static_cast<f64>(bytes)));
}

void upscale2_catmull_rom(const u8* src, u32 w, u32 h, u8* dst) noexcept {
    // Os dois deslocamentos de fase do ×2: −0,25 e +0,25 pixel de origem.
    // Pesos de Catmull-Rom (a = −0,5) para t = 0,75 (pares) e 0,25 (ímpares).
    // Par 2k: s = k − 0,25 (piso k−1, t = 0,75), taps k−2..k+1.
    // Ímpar 2k+1: s = k + 0,25 (piso k, t = 0,25), taps k−1..k+2.
    static constexpr f32 kEven[4] = {-0.0234375f, 0.2265625f, 0.8671875f, -0.0703125f};
    static constexpr f32 kOdd[4] = {-0.0703125f, 0.8671875f, 0.2265625f, -0.0234375f};
    const u32 W = w * 2, H = h * 2;
    auto clampi = [](i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; };
    // Linha intermediária (horizontal) para as 4 linhas de origem de cada saída.
    std::vector<f32> rows;
    try { rows.resize(static_cast<usize>(W) * 3 * 4); } catch (...) { return; }
    i32 cached[4] = {-100, -100, -100, -100};
    auto hrow = [&](i32 sy) -> const f32* {
        sy = clampi(sy, 0, static_cast<i32>(h) - 1);
        for (int k = 0; k < 4; ++k) if (cached[k] == sy) return rows.data() + static_cast<usize>(k) * W * 3;
        int slot = 0;
        for (int k = 0; k < 4; ++k) if (cached[k] < cached[slot]) slot = k;
        cached[slot] = sy;
        f32* out = rows.data() + static_cast<usize>(slot) * W * 3;
        const u8* in = src + static_cast<usize>(sy) * w * 3;
        for (u32 X = 0; X < W; ++X) {
            const i32 base = (X & 1u) ? static_cast<i32>(X / 2) - 1 : static_cast<i32>(X / 2) - 2;
            const f32* k = (X & 1u) ? kOdd : kEven;
            for (u32 c = 0; c < 3; ++c) {
                f32 acc = 0.0f;
                for (int t = 0; t < 4; ++t) acc += k[t] * in[clampi(base + t, 0, static_cast<i32>(w) - 1) * 3 + c];
                out[X * 3 + c] = acc;
            }
        }
        return out;
    };
    for (u32 Y = 0; Y < H; ++Y) {
        const i32 base = (Y & 1u) ? static_cast<i32>(Y / 2) - 1 : static_cast<i32>(Y / 2) - 2;
        const f32* k = (Y & 1u) ? kOdd : kEven;
        const f32* r[4] = {hrow(base), hrow(base + 1), hrow(base + 2), hrow(base + 3)};
        u8* out = dst + static_cast<usize>(Y) * W * 3;
        for (u32 i = 0; i < W * 3; ++i) {
            const f32 v = k[0] * r[0][i] + k[1] * r[1][i] + k[2] * r[2][i] + k[3] * r[3][i] + 0.5f;
            out[i] = static_cast<u8>(v <= 0.0f ? 0 : v >= 255.0f ? 255 : static_cast<int>(v));
        }
    }
}

// --- Tiles -------------------------------------------------------------------------------
Status TiledRunner::prepare(const TileConfig& config, u32 width, u32 height, u32 workers, u64 budgetPerWorker) noexcept try {
    if (!width || !height || config.tile < 16 || config.netScale < 1 || config.outDiv < 1 ||
        (config.netScale * 2) % config.outDiv != 0)
        return Errc::InvalidArgument;
    cfg_ = config;
    w_ = width;
    h_ = height;
    outW_ = width * cfg_.netScale / cfg_.outDiv;
    outH_ = height * cfg_.netScale / cfg_.outDiv;
    tiles_.clear();
    usize sig = 0;
    for (u32 y = 0; y < height; y += cfg_.tile)
        for (u32 x = 0; x < width; x += cfg_.tile) {
            Tile t{};
            t.x = x; t.y = y;
            t.w = std::min(cfg_.tile, width - x);
            t.h = std::min(cfg_.tile, height - y);
            t.ex0 = x > cfg_.margin ? x - cfg_.margin : 0;
            t.ey0 = y > cfg_.margin ? y - cfg_.margin : 0;
            t.ex1 = std::min(width, x + t.w + cfg_.margin);
            t.ey1 = std::min(height, y + t.h + cfg_.margin);
            // Começo da borda alinhado ao passo da média em caixa da saída.
            t.sigOffset = sig;
            if (cfg_.skipStatic) sig += static_cast<usize>((t.ex1 - t.ex0 + 1) / 2) * ((t.ey1 - t.ey0 + 1) / 2) * 3;
            tiles_.push_back(t);
        }
    signatures_.assign(sig, 0);
    if (cfg_.temporalBlend) {
        previous_.assign(static_cast<usize>(width) * height * 3, 0);
        stableMask_.assign(static_cast<usize>(width) * height, 0);
    } else {
        std::vector<u8>().swap(previous_);
        std::vector<u8>().swap(stableMask_);
    }
    tileStatus_.assign(tiles_.size(), 0);
    allocators_.clear();
    scratch_.clear();
    fresh_.clear();
    const u32 ext = cfg_.tile + 2 * cfg_.margin;
    const u32 core = cfg_.tile * cfg_.netScale / cfg_.outDiv;
    for (u32 i = 0; i < std::max(workers, 1u); ++i) {
        allocators_.push_back(std::make_unique<BoundedAllocator>(budgetPerWorker));
        scratch_.emplace_back(static_cast<usize>(ext) * ext * 3);
        fresh_.emplace_back(cfg_.temporalBlend ? static_cast<usize>(core) * core * 3 : 0);
    }
    history_ = false;
    return OkStatus;
} catch (const std::bad_alloc&) {
    return Status{Errc::OutOfMemory, "buffers dos tiles da rede"};
}

u64 TiledRunner::history_bytes() const noexcept {
    return signatures_.size() + previous_.size() + stableMask_.size();
}

u64 TiledRunner::peak_bytes() const noexcept {
    u64 peak = history_bytes();
    for (const auto& a : allocators_) peak += a->peak();
    for (const auto& s : scratch_) peak += s.size();
    for (const auto& s : fresh_) peak += s.size();
    return peak;
}

bool TiledRunner::is_static_tile(const Tile& t, const u8* rgb) const noexcept {
    const u8* sig = signatures_.data() + t.sigOffset;
    u64 sum = 0, count = 0;
    for (u32 y = t.ey0; y < t.ey1; y += 2)
        for (u32 x = t.ex0; x < t.ex1; x += 2)
            for (u32 c = 0; c < 3; ++c) {
                const u32 x1 = std::min(x + 1, w_ - 1), y1 = std::min(y + 1, h_ - 1);
                const u32 v = (rgb[(static_cast<usize>(y) * w_ + x) * 3 + c] + rgb[(static_cast<usize>(y) * w_ + x1) * 3 + c] +
                               rgb[(static_cast<usize>(y1) * w_ + x) * 3 + c] + rgb[(static_cast<usize>(y1) * w_ + x1) * 3 + c] + 2) / 4;
                const u32 d = static_cast<u32>(std::abs(static_cast<int>(v) - static_cast<int>(*sig++)));
                if (d > cfg_.skipMax) return false;
                sum += d;
                ++count;
            }
    return count && static_cast<f32>(sum) <= cfg_.skipMean * static_cast<f32>(count);
}

void TiledRunner::store_signature(const Tile& t, const u8* rgb) noexcept {
    u8* sig = signatures_.data() + t.sigOffset;
    for (u32 y = t.ey0; y < t.ey1; y += 2)
        for (u32 x = t.ex0; x < t.ex1; x += 2)
            for (u32 c = 0; c < 3; ++c) {
                const u32 x1 = std::min(x + 1, w_ - 1), y1 = std::min(y + 1, h_ - 1);
                *sig++ = static_cast<u8>((rgb[(static_cast<usize>(y) * w_ + x) * 3 + c] + rgb[(static_cast<usize>(y) * w_ + x1) * 3 + c] +
                                          rgb[(static_cast<usize>(y1) * w_ + x) * 3 + c] + rgb[(static_cast<usize>(y1) * w_ + x1) * 3 + c] + 2) / 4);
            }
}

bool TiledRunner::run_tile(NetRunner& net, const Tile& t, const u8* rgb, u8* out, u32 worker, bool blend) noexcept try {
    const u32 ew = t.ex1 - t.ex0, eh = t.ey1 - t.ey0;
    std::vector<u8>& input = scratch_[worker];
    for (u32 row = 0; row < eh; ++row)
        std::memcpy(input.data() + static_cast<usize>(row) * ew * 3, rgb + (static_cast<usize>(t.ey0 + row) * w_ + t.ex0) * 3, ew * 3u);
    BoundedAllocator* allocator = allocators_[worker].get();
    ncnn::Mat in = ncnn::Mat::from_pixels(input.data(), ncnn::Mat::PIXEL_RGB, static_cast<int>(ew), static_cast<int>(eh), allocator);
    if (in.empty()) return false;
    static constexpr float kNorm[3] = {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f};
    in.substract_mean_normalize(nullptr, kNorm);
    ncnn::Mat result;
    if (!net.infer(in, result, allocator)) return false;
    if (result.w != static_cast<int>(ew * cfg_.netScale) || result.h != static_cast<int>(eh * cfg_.netScale) || result.c != 3)
        return false;
    const u32 div = cfg_.outDiv;
    const u32 ox = t.x * cfg_.netScale / div, oy = t.y * cfg_.netScale / div;
    const u32 ow = t.w * cfg_.netScale / div, oh = t.h * cfg_.netScale / div;
    const u32 sx = (t.x - t.ex0) * cfg_.netScale / div, sy = (t.y - t.ey0) * cfg_.netScale / div;
    const u32 outStride = outW_ * 3;
    if (!blend) {
        mat_to_rgb(result, div, sx, sy, ow, oh, out + static_cast<usize>(oy) * outStride + ox * 3, outStride);
        return true;
    }
    // Mistura temporal: nos pixels parados (entrada igual ao quadro anterior
    // num 3×3) e com a saída nova perto da anterior, média com a anterior —
    // tira o tremido do detalhe inventado sem arrastar o que se move.
    std::vector<u8>& fresh = fresh_[worker];
    mat_to_rgb(result, div, sx, sy, ow, oh, fresh.data(), ow * 3);
    for (u32 y = 0; y < oh; ++y) {
        u8* dst = out + static_cast<usize>(oy + y) * outStride + ox * 3;
        const u8* src = fresh.data() + static_cast<usize>(y) * ow * 3;
        const u32 ly = std::min(h_ - 1, (oy + y) * div / cfg_.netScale);
        for (u32 x = 0; x < ow; ++x) {
            const u32 lx = std::min(w_ - 1, (ox + x) * div / cfg_.netScale);
            const bool still = stableMask_[static_cast<usize>(ly) * w_ + lx] != 0;
            for (u32 c = 0; c < 3; ++c) {
                const int n = src[x * 3 + c], o = dst[x * 3 + c];
                dst[x * 3 + c] = static_cast<u8>(still && std::abs(n - o) <= 16 ? (n + o + 1) / 2 : n);
            }
        }
    }
    return true;
} catch (...) {
    return false;
}

Status TiledRunner::run(NetRunner& net, WorkerPool& pool, const u8* rgb, u8* out, const StageContext& ctx) noexcept {
    if (!rgb || !out || tiles_.empty() || !net.loaded()) return Errc::InvalidState;
    blendFrame_ = false;
    if (cfg_.temporalBlend && history_) {
        const usize n = static_cast<usize>(w_) * h_;
        usize changed = 0;
        for (usize p = 0; p < n; ++p) {
            int d = 0;
            for (u32 c = 0; c < 3; ++c) d = std::max(d, std::abs(int(rgb[p * 3 + c]) - int(previous_[p * 3 + c])));
            stableMask_[p] = d <= 2;
            changed += d > 12;
        }
        // Corte ou movimento grande: nada de mistura neste quadro.
        blendFrame_ = changed * 4 <= n;
        if (blendFrame_) {
            // Erosão 3×3 no lugar (linha anterior guardada em `previous_`, que
            // é reescrita no fim do quadro): marca 2 = parado nos vizinhos.
            for (u32 y = 0; y < h_; ++y)
                for (u32 x = 0; x < w_; ++x) {
                    bool still = true;
                    for (u32 yy = y ? y - 1 : 0; yy <= std::min(y + 1, h_ - 1) && still; ++yy)
                        for (u32 xx = x ? x - 1 : 0; xx <= std::min(x + 1, w_ - 1); ++xx)
                            if (!(stableMask_[static_cast<usize>(yy) * w_ + xx] & 1u)) { still = false; break; }
                    if (still) stableMask_[static_cast<usize>(y) * w_ + x] |= 2u;
                }
            for (usize p = 0; p < n; ++p) stableMask_[p] = (stableMask_[p] & 2u) ? 1 : 0;
        }
    }
    const bool blend = blendFrame_;
    std::atomic<u32> done{0};
    std::atomic<bool> failed{false};
    const u32 total = static_cast<u32>(tiles_.size());
    auto job = [&](u32 index, u32 worker) {
        if (failed.load(std::memory_order_relaxed) || ctx.cancelled()) { tileStatus_[index] = 0; return; }
        const Tile& t = tiles_[index];
        if (cfg_.skipStatic && history_ && is_static_tile(t, rgb)) {
            tileStatus_[index] = 2;
        } else if (run_tile(net, t, rgb, out, worker, blend)) {
            tileStatus_[index] = 1;
            if (cfg_.skipStatic) store_signature(t, rgb);
        } else {
            tileStatus_[index] = 0;
            failed.store(true, std::memory_order_relaxed);
        }
        const u32 finished = done.fetch_add(1, std::memory_order_relaxed) + 1;
        if (worker == 0) ctx.progress(static_cast<f32>(finished) / static_cast<f32>(total));
    };
    for (auto& a : allocators_) a->reset_peak();
    pool.run(total, job);
    if (ctx.cancelled()) { history_ = false; return Errc::Cancelled; }
    if (failed.load()) {
        // Vulkan falhou num tile: recarrega na CPU e refaz só os que faltam.
        if (!net.can_fallback() || !net.fallback_to_cpu().ok()) {
            history_ = false;
            bool budget = false;
            for (auto& a : allocators_) budget = budget || a->exceeded();
            return budget ? Status{Errc::BudgetExceeded, "a rede passou do orcamento de memoria num tile"}
                          : Status{Errc::InvalidState, "a rede falhou num tile"};
        }
        for (u32 i = 0; i < total; ++i) {
            if (tileStatus_[i] != 0) continue;
            if (ctx.cancelled()) { history_ = false; return Errc::Cancelled; }
            if (!run_tile(net, tiles_[i], rgb, out, 0, blend)) { history_ = false; return Status{Errc::InvalidState, "a rede falhou num tile (CPU)"}; }
            tileStatus_[i] = 1;
            if (cfg_.skipStatic) store_signature(tiles_[i], rgb);
            ctx.beat();
        }
    }
    skipped_ = 0;
    for (u8 s : tileStatus_) skipped_ += s == 2;
    if (cfg_.temporalBlend) std::memcpy(previous_.data(), rgb, previous_.size());
    history_ = true;
    return OkStatus;
}

} // namespace aurea::hyper::neural
