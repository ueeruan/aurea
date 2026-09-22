// =============================================================================
//  Aurea / audio / Waveform.cpp — picos multirresolução, calculados uma vez.
//
//  Fase 8D: e guardados em disco. Um áudio de 30 min leva segundos de decode
//  no celular (a thread de fundo esquentando a cada abertura do projeto); os
//  picos do nível 0 são 200 bytes por segundo (360 KB para 30 min). A próxima
//  abertura lê o arquivo e monta os níveis em milissegundos.
// =============================================================================
#include "aurea/audio/Audio.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/core/Time.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace aurea::audio {

namespace {
constexpr char kDiskMagic[4] = {'A', 'W', 'V', '1'};

/// Níveis mais grossos a partir do 0: máximo de pares até sobrar um balde.
void build_levels(std::vector<std::vector<u8>>& levels) {
    levels.resize(1);
    while (levels.back().size() > 1) {
        const std::vector<u8>& prev = levels.back();
        std::vector<u8> next((prev.size() + 1) / 2);
        for (usize i = 0; i < next.size(); ++i) {
            next[i] = std::max(prev[2 * i], 2 * i + 1 < prev.size() ? prev[2 * i + 1] : u8{0});
        }
        levels.push_back(std::move(next));
    }
}

u64 fnv1a(const void* data, usize n, u64 h = 1469598103934665603ull) {
    const auto* p = static_cast<const u8*>(data);
    for (usize i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
} // namespace

WaveformCache::WaveformCache(VideoSourceFactory* factory) : factory_(factory) {
    thread_ = std::thread([this] { thread_main(); });
}

WaveformCache::~WaveformCache() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void WaveformCache::set_disk_directory(std::string dir) {
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
        if (ec) dir.clear();   // sem pasta: calcula como antes, sem guardar
    }
    std::lock_guard<std::mutex> lock(mutex_);
    diskDir_ = std::move(dir);
}

std::string WaveformCache::disk_path(const AudioAssetRef& ref) const {
    if (diskDir_.empty() || ref.path.empty()) return {};
    // Caminho + duração + tamanho do arquivo (content:// não tem tamanho aqui:
    // fica caminho + duração, que já muda quando a mídia troca de verdade).
    std::error_code ec;
    const u64 size = static_cast<u64>(std::filesystem::file_size(std::filesystem::u8path(ref.path), ec));
    u64 h = fnv1a(ref.path.data(), ref.path.size());
    h = fnv1a(&ref.durationSamples, sizeof(ref.durationSamples), h);
    if (!ec) h = fnv1a(&size, sizeof(size), h);
    char name[40];
    std::snprintf(name, sizeof(name), "%016llx.awv", static_cast<unsigned long long>(h));
    return diskDir_ + "/" + name;
}

bool WaveformCache::load_disk(const std::string& file, i64 total, std::vector<u8>& out) {
    std::FILE* f = std::fopen(file.c_str(), "rb");
    if (!f) return false;
    char magic[4] = {};
    i64 n = -1;
    bool ok = std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, kDiskMagic, 4) == 0
           && std::fread(&n, sizeof(n), 1, f) == 1 && n == total && n > 0;
    if (ok) {
        out.assign(static_cast<usize>(n), u8{0});
        ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    }
    std::fclose(f);
    if (!ok) {
        // Arquivo truncado/de outra versão: apaga e recalcula (cache corrompido é refeito).
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(file), ec);
    }
    return ok;
}

void WaveformCache::save_disk(const std::string& file, const std::vector<u8>& level0) {
    // Escrita atômica: temporário + rename (um cache pela metade nunca é lido).
    const std::string tmp = file + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    const i64 n = static_cast<i64>(level0.size());
    bool ok = std::fwrite(kDiskMagic, 1, 4, f) == 4 && std::fwrite(&n, sizeof(n), 1, f) == 1
           && std::fwrite(level0.data(), 1, level0.size(), f) == level0.size();
    ok = std::fclose(f) == 0 && ok;
    std::error_code ec;
    if (ok) std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(file), ec);
    if (!ok || ec) {
        AUREA_LOG_WARN("waveform: cache em disco nao gravado");
        std::filesystem::remove(std::filesystem::u8path(tmp), ec);
    }
}

void WaveformCache::request(u64 key, const AudioAssetRef& ref) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(key);
        if (it != entries_.end() && it->second.ref.path == ref.path) return;
        Entry e;
        e.ref = ref;
        e.total = std::max<i64>(1, (ref.durationSamples + kBaseSamples - 1) / kBaseSamples);
        e.levels.emplace_back(static_cast<usize>(e.total), u8{0});
        entries_[key] = std::move(e);
        queue_.push_back(key);
    }
    wake_.notify_one();
}

bool WaveformCache::query(u64 key, f64 srcStart, f64 samplesPerBucket, u32 count, u8* out) const {
    std::fill(out, out + count, u8{0});
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.failed) return false;
    const Entry& e = it->second;
    // Nível: o mais grosso cujo balde ainda cabe no balde pedido.
    u32 level = 0;
    if (e.done) {
        while (level + 1 < e.levels.size()
               && static_cast<f64>(kBaseSamples << (level + 1)) <= samplesPerBucket) {
            ++level;
        }
    }
    const std::vector<u8>& lv = e.levels[level];
    const f64 unit = static_cast<f64>(kBaseSamples << level);
    const i64 n = static_cast<i64>(lv.size());
    const i64 readyAt = e.done ? n : (e.ready >> level);
    for (u32 b = 0; b < count; ++b) {
        const f64 a = (srcStart + b * samplesPerBucket) / unit;
        const f64 z = (srcStart + (b + 1) * samplesPerBucket) / unit;
        i64 i0 = static_cast<i64>(std::floor(a));
        i64 i1 = std::max(i0 + 1, static_cast<i64>(std::ceil(z)));
        i0 = std::max<i64>(0, i0);
        i1 = std::min(i1, readyAt);
        u8 m = 0;
        for (i64 i = i0; i < i1; ++i) m = std::max(m, lv[static_cast<usize>(i)]);
        out[b] = m;
    }
    return true;
}

f32 WaveformCache::progress(u64 key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.failed) return -1.0f;
    return it->second.done ? 1.0f : static_cast<f32>(it->second.ready) / static_cast<f32>(it->second.total);
}

void WaveformCache::thread_main() {
    set_current_thread_name("aurea-waveform");
    set_current_thread_priority(ThreadPriority::Background);
    std::unique_lock<std::mutex> lock(mutex_);
    while (!quit_) {
        wake_.wait(lock, [this] { return quit_ || !queue_.empty(); });
        if (quit_) break;
        const u64 key = queue_.front();
        queue_.erase(queue_.begin());
        auto it = entries_.find(key);
        if (it == entries_.end() || it->second.done) continue;
        const AudioAssetRef ref = it->second.ref;
        const i64 total = it->second.total;
        const std::string disk = disk_path(ref);
        lock.unlock();

        const u64 t0 = monotonic_ns();
        // Já calculada numa sessão anterior: lê do disco em vez de decodificar.
        std::vector<u8> stored;
        if (!disk.empty() && load_disk(disk, total, stored)) {
            lock.lock();
            auto e = entries_.find(key);
            if (e != entries_.end() && e->second.ref.path == ref.path && e->second.total == total) {
                e->second.levels.assign(1, std::move(stored));
                build_levels(e->second.levels);
                e->second.ready = e->second.total;
                e->second.done = true;
                AUREA_LOG_INFO("waveform: %.1f s de audio lidos do cache em %.1f ms",
                               static_cast<f64>(total * kBaseSamples) / kMixRate,
                               static_cast<f64>(monotonic_ns() - t0) / 1e6);
            }
            generation_.fetch_add(1, std::memory_order_acq_rel);
            continue;
        }

        // Decoder próprio (cache sem thread, pequeno): não disputa com o som
        // do preview nem com o export.
        AudioBlockCache blocks(factory_, 2ull << 20, false);
        blocks.register_asset(key, ref);
        constexpr i64 kPerBlock = kBlockFrames / kBaseSamples;
        std::vector<u8> chunk(static_cast<usize>(kPerBlock));
        bool failed = false;
        i64 done = 0;
        u32 sinceBump = 0;
        for (i64 b = 0; done < total; ++b) {
            auto blk = blocks.fetch(key, b);
            if (!blk) {
                failed = b == 0;
                break;
            }
            const i64 n = std::min<i64>(kPerBlock, total - done);
            for (i64 i = 0; i < n; ++i) {
                f32 peak = 0.0f;
                const f32* p = blk->pcm.data() + static_cast<usize>(i * kBaseSamples) * 2;
                for (u32 s = 0; s < kBaseSamples * 2; ++s) peak = std::max(peak, std::fabs(p[s]));
                chunk[static_cast<usize>(i)] = static_cast<u8>(std::lround(255.0f * std::sqrt(std::min(1.0f, peak))));
            }
            {
                std::lock_guard<std::mutex> g(mutex_);
                auto e = entries_.find(key);
                if (e == entries_.end() || quit_) break;
                std::copy(chunk.begin(), chunk.begin() + n, e->second.levels[0].begin() + done);
                done += n;
                e->second.ready = done;
            }
            // A UI redesenha a cada ~2 s de áudio pronto, não a cada bloco.
            if (++sinceBump >= 4) {
                sinceBump = 0;
                generation_.fetch_add(1, std::memory_order_acq_rel);
            }
            if (quit_) break;
        }
        lock.lock();
        auto e = entries_.find(key);
        if (e != entries_.end()) {
            if (failed) {
                e->second.failed = true;
                AUREA_LOG_WARN("waveform: audio ilegivel");
            } else {
                build_levels(e->second.levels);
                e->second.done = true;
                e->second.ready = e->second.total;
                AUREA_LOG_INFO("waveform: %.1f s de audio em %.0f ms", static_cast<f64>(total * kBaseSamples) / kMixRate,
                               static_cast<f64>(monotonic_ns() - t0) / 1e6);
                // Só o cálculo COMPLETO vai para o disco (parado no meio = recalcula).
                if (!disk.empty() && done >= total && !quit_) {
                    std::vector<u8> level0 = e->second.levels[0];
                    lock.unlock();
                    save_disk(disk, level0);
                    lock.lock();
                }
            }
        }
        generation_.fetch_add(1, std::memory_order_acq_rel);
    }
}

} // namespace aurea::audio
