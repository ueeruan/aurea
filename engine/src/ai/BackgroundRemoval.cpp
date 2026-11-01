// =============================================================================
//  Aurea / ai / BackgroundRemoval.cpp — ver BackgroundRemoval.hpp
// =============================================================================
#include "aurea/ai/BackgroundRemoval.hpp"

#include "aurea/ai/ForegroundMatte.hpp"
#include "aurea/ai/RotoMatte.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/project/FileIO.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <system_error>

namespace aurea::ai {
namespace {

constexpr usize kCells = static_cast<usize>(kBgMatteSize) * kBgMatteSize;
constexpr char kMagic[8] = {'A', 'U', 'B', 'G', 'M', 'S', 'K', '1'};
/// Teto do arquivo lido (o cache cheio cabe com folga).
constexpr usize kMaxFileBytes = 512u * 1024u * 1024u;

std::atomic<BgNativeSegmenter> gSegmenter{nullptr};
std::atomic<void*> gSegmenterCtx{nullptr};

u64 mix(u64 h, u64 v) noexcept {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    return h ^ (h >> 29);
}

u64 fnv(const u8* p, usize n, u64 h = 0xCBF29CE484222325ull) noexcept {
    for (usize i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}

void put_u32(std::vector<u8>& o, u32 v) { for (u32 i = 0; i < 4; ++i) o.push_back(static_cast<u8>(v >> (8 * i))); }
void put_u64(std::vector<u8>& o, u64 v) { for (u32 i = 0; i < 8; ++i) o.push_back(static_cast<u8>(v >> (8 * i))); }

struct Reader {
    const u8* p; usize n; usize at = 0;
    bool u32v(u32& v) noexcept {
        if (n - at < 4) return false;
        v = 0; for (u32 i = 0; i < 4; ++i) v |= static_cast<u32>(p[at + i]) << (8 * i);
        at += 4; return true;
    }
    bool u64v(u64& v) noexcept {
        if (n - at < 8) return false;
        v = 0; for (u32 i = 0; i < 8; ++i) v |= static_cast<u64>(p[at + i]) << (8 * i);
        at += 8; return true;
    }
};

/// O RLE cobre exatamente a grade (o que o roto_unpack aceitaria).
bool rle_valid(const u8* p, usize n) noexcept {
    if (n % 2) return false;
    usize total = 0;
    for (usize i = 0; i < n; i += 2) {
        if (!p[i + 1]) return false;
        total += p[i + 1];
        if (total > kCells) return false;
    }
    return total == kCells;
}

/// A mesma quantização do cache: calcular e reler do cache dão o mesmo valor.
void quantize(std::vector<f32>& matte) {
    std::vector<u8> packed;
    roto_pack(matte, packed);
    (void)roto_unpack(packed, matte.size(), matte);
}

u64 raw_slot_key(u64 matteKey, i64 frame) noexcept { return mix(mix(matteKey, 0xB6A3u), static_cast<u64>(frame)); }
u64 refined_slot_key(u64 matteKey, i64 frame) noexcept { return mix(mix(matteKey, 0xB6F1u), static_cast<u64>(frame)); }

} // namespace

// --- Segmentador nativo --------------------------------------------------------
void bg_set_native_segmenter(BgNativeSegmenter fn, void* ctx) noexcept {
    gSegmenterCtx.store(ctx, std::memory_order_release);
    gSegmenter.store(fn, std::memory_order_release);
}

bool bg_native_available(u32 model) noexcept {
    return model == kBgModelPerson && gSegmenter.load(std::memory_order_acquire) != nullptr;
}

u32 bg_effective_model(u32 requested) noexcept {
    return requested == kBgModelPerson && bg_native_available(kBgModelPerson) ? kBgModelPerson : kBgModelAuto;
}

// --- Recorte de um quadro --------------------------------------------------------
void bg_matte(const BgRaw* prev, const BgRaw& cur, const BgRaw* next, std::vector<f32>& out) {
    if (!cur.valid()) { out.assign(kCells, 0.0f); return; }
    static const std::vector<f32> kNoProb;
    static const std::vector<u8> kNoRgb;
    const bool hp = prev && prev->valid(), hn = next && next->valid();
    // 1) Tempo: mediana de 3 quadros, com busca local de ±2 células e corte
    //    rejeitado (o vizinho de outra cena não traz silhueta velha).
    std::vector<f32> stable;
    stabilize_foreground(cur.prob, cur.rgb, hp ? prev->prob : kNoProb, hp ? prev->rgb : kNoRgb,
                         hn ? next->prob : kNoProb, hn ? next->rgb : kNoRgb, kBgMatteSize, stable);
    // 2) Espaço: a rede confiante vira semente e a faixa incerta é decidida
    //    pelas arestas do quadro (a borda para onde a cor muda).
    roto_segment(stable.data(), cur.rgb.data(), nullptr, nullptr, kBgMatteSize, out);
    quantize(out);
}

u64 bg_matte_key(u64 contentHash, const std::string& sourcePath, u32 width, u32 height, i64 frameCount,
                 u32 model) noexcept {
    u64 h = contentHash ? mix(0xB6C0u, contentHash)
                        : mix(0xB6C1u, fnv(reinterpret_cast<const u8*>(sourcePath.data()), sourcePath.size()));
    h = mix(h, (static_cast<u64>(width) << 32) | height);
    h = mix(h, static_cast<u64>(std::max<i64>(0, frameCount)));
    h = mix(h, model);
    h = mix(h, kBgAlgorithmVersion);
    return h ? h : 1;
}

// --- BgMatteStore --------------------------------------------------------------
std::shared_ptr<BgMatteStore> make_bg_matte_store() {
    return std::make_shared<BgMatteStore>();
}

void BgMatteStore::ensure_loaded_locked() {
    if (loaded_) return;
    loaded_ = true;
    if (path_.empty() || !fileio::exists(path_)) return;
    std::vector<u8> bytes;
    if (!fileio::read_all(path_, bytes, kMaxFileBytes)) {
        AUREA_LOG_WARN("Remover fundo: nao deu para ler as mascaras salvas");
        return;
    }
    // decode() trava o mutex: aqui ele já está travado, então decodifica
    // num store temporário e junta.
    BgMatteStore tmp;
    if (!tmp.decode(bytes.data(), bytes.size())) {
        AUREA_LOG_WARN("Remover fundo: mascaras salvas ilegiveis (%zu bytes); serao recalculadas", bytes.size());
        return;
    }
    for (auto& [key, track] : tmp.tracks_) {
        Track& mine = tracks_[key];
        for (auto& [frame, rle] : track.frames) {
            if (mine.frames.count(frame)) continue;
            bytes_ += rle.size();
            mine.frames.emplace(frame, std::move(rle));
        }
    }
    evict_locked(0, -1);
}

void BgMatteStore::evict_locked(u64 keepKey, i64 keepFrame) {
    // 1) Faixas usadas há mais tempo, inteiras (nunca a que está sendo escrita).
    while (bytes_ > maxBytes_ && tracks_.size() > 1) {
        auto old = tracks_.end();
        for (auto it = tracks_.begin(); it != tracks_.end(); ++it) {
            if (it->first == keepKey) continue;
            if (old == tracks_.end() || it->second.stamp < old->second.stamp) old = it;
        }
        if (old == tracks_.end()) break;
        for (const auto& [f, rle] : old->second.frames) bytes_ -= std::min(bytes_, rle.size());
        tracks_.erase(old);
    }
    // 2) Uma faixa só maior que o teto: saem os quadros mais longe do atual.
    while (bytes_ > maxBytes_) {
        auto t = tracks_.find(keepKey);
        if (t == tracks_.end() || t->second.frames.size() <= 1) break;
        auto far = t->second.frames.end();
        i64 dist = -1;
        for (auto it = t->second.frames.begin(); it != t->second.frames.end(); ++it) {
            const i64 d = std::llabs(it->first - keepFrame);
            if (it->first != keepFrame && d > dist) { dist = d; far = it; }
        }
        if (far == t->second.frames.end()) break;
        bytes_ -= std::min(bytes_, far->second.size());
        t->second.frames.erase(far);
    }
}

bool BgMatteStore::get(u64 key, i64 frame, std::vector<f32>& out) {
    std::vector<u8> packed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ensure_loaded_locked();
        auto t = tracks_.find(key);
        if (t == tracks_.end()) return false;
        auto f = t->second.frames.find(frame);
        if (f == t->second.frames.end()) return false;
        t->second.stamp = ++stamp_;
        packed = f->second;
    }
    return roto_unpack(packed, kCells, out);
}

bool BgMatteStore::has(u64 key, i64 frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_loaded_locked();
    auto t = tracks_.find(key);
    return t != tracks_.end() && t->second.frames.count(frame) != 0;
}

i64 BgMatteStore::count(u64 key, i64 first, i64 last) {
    if (last < first) std::swap(first, last);
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_loaded_locked();
    auto t = tracks_.find(key);
    if (t == tracks_.end()) return 0;
    i64 n = 0;
    if (static_cast<u64>(last - first) > t->second.frames.size()) {
        for (const auto& [f, rle] : t->second.frames) n += (f >= first && f <= last) ? 1 : 0;
    } else {
        for (i64 f = first; f <= last; ++f) n += t->second.frames.count(f) ? 1 : 0;
    }
    return n;
}

void BgMatteStore::put(u64 key, i64 frame, const std::vector<f32>& matte) {
    if (matte.size() != kCells) return;
    std::vector<u8> packed;
    roto_pack(matte, packed);
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_loaded_locked();
    Track& t = tracks_[key];
    t.stamp = ++stamp_;
    auto& slot = t.frames[frame];
    bytes_ -= std::min(bytes_, slot.size());
    bytes_ += packed.size();
    slot = std::move(packed);
    dirty_.store(true, std::memory_order_relaxed);
    if (bytes_ > maxBytes_) evict_locked(key, frame);
}

void BgMatteStore::erase(u64 key) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_loaded_locked();
    auto t = tracks_.find(key);
    if (t == tracks_.end()) return;
    for (const auto& [f, rle] : t->second.frames) bytes_ -= std::min(bytes_, rle.size());
    tracks_.erase(t);
    dirty_.store(true, std::memory_order_relaxed);
}

void BgMatteStore::retain(const std::vector<u64>& keys) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_loaded_locked();
    for (auto it = tracks_.begin(); it != tracks_.end();) {
        if (std::find(keys.begin(), keys.end(), it->first) != keys.end()) { ++it; continue; }
        for (const auto& [f, rle] : it->second.frames) bytes_ -= std::min(bytes_, rle.size());
        it = tracks_.erase(it);
        dirty_.store(true, std::memory_order_relaxed);
    }
}

usize BgMatteStore::bytes() {
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_;
}

usize BgMatteStore::frames() {
    std::lock_guard<std::mutex> lock(mutex_);
    usize n = 0;
    for (const auto& [k, t] : tracks_) n += t.frames.size();
    return n;
}

void BgMatteStore::set_max_bytes(usize bytes) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    maxBytes_ = std::max<usize>(bytes, 1);
    if (bytes_ > maxBytes_) evict_locked(0, -1);
}

void BgMatteStore::encode(std::vector<u8>& out) {
    out.clear();
    out.insert(out.end(), kMagic, kMagic + 8);
    put_u32(out, kBgAlgorithmVersion);
    put_u32(out, kBgMatteSize);
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_loaded_locked();
    out.reserve(out.size() + bytes_ + tracks_.size() * 16 + 64);
    u32 nonEmpty = 0;
    for (const auto& [k, t] : tracks_) nonEmpty += t.frames.empty() ? 0u : 1u;
    put_u32(out, nonEmpty);
    // Ordem estável (chave, quadro): o mesmo cache dá o mesmo arquivo.
    std::vector<u64> keys;
    for (const auto& [k, t] : tracks_) if (!t.frames.empty()) keys.push_back(k);
    std::sort(keys.begin(), keys.end());
    for (const u64 k : keys) {
        const Track& t = tracks_.at(k);
        put_u64(out, k);
        put_u32(out, static_cast<u32>(t.frames.size()));
        std::vector<i64> frames;
        frames.reserve(t.frames.size());
        for (const auto& [f, rle] : t.frames) frames.push_back(f);
        std::sort(frames.begin(), frames.end());
        for (const i64 f : frames) {
            const std::vector<u8>& rle = t.frames.at(f);
            put_u64(out, static_cast<u64>(f));
            put_u32(out, static_cast<u32>(rle.size()));
            out.insert(out.end(), rle.begin(), rle.end());
        }
    }
    put_u64(out, fnv(out.data(), out.size()));
}

bool BgMatteStore::decode(const u8* data, usize size) {
    if (!data || size < 8 + 12 + 8 || std::memcmp(data, kMagic, 8) != 0) return false;
    const usize body = size - 8;
    u64 sum = 0;
    for (u32 i = 0; i < 8; ++i) sum |= static_cast<u64>(data[body + i]) << (8 * i);
    if (sum != fnv(data, body)) return false;
    Reader r{data, body, 8};
    u32 version = 0, grid = 0, tracks = 0;
    if (!r.u32v(version) || !r.u32v(grid) || !r.u32v(tracks)) return false;
    // Outra versão do algoritmo ou outra grade: o recorte salvo não é o que
    // o motor calcularia agora. Recalcula (nunca mistura os dois).
    if (version != kBgAlgorithmVersion || grid != kBgMatteSize) return false;
    std::unordered_map<u64, Track> parsed;
    for (u32 t = 0; t < tracks; ++t) {
        u64 key = 0; u32 frames = 0;
        if (!r.u64v(key) || !r.u32v(frames)) return false;
        if (frames > (body - r.at) / 12) return false;
        Track& track = parsed[key];
        for (u32 i = 0; i < frames; ++i) {
            u64 f = 0; u32 len = 0;
            if (!r.u64v(f) || !r.u32v(len) || len > body - r.at || !rle_valid(data + r.at, len)) return false;
            track.frames[static_cast<i64>(f)] = std::vector<u8>(data + r.at, data + r.at + len);
            r.at += len;
        }
    }
    if (r.at != body) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [key, track] : parsed) {
        Track& mine = tracks_[key];
        for (auto& [frame, rle] : track.frames) {
            if (mine.frames.count(frame)) continue;
            bytes_ += rle.size();
            mine.frames.emplace(frame, std::move(rle));
        }
    }
    if (bytes_ > maxBytes_) evict_locked(0, -1);
    return true;
}

void BgMatteStore::attach(std::string path) {
    std::lock_guard<std::mutex> lock(mutex_);
    tracks_.clear();
    bytes_ = 0;
    path_ = std::move(path);
    loaded_ = path_.empty();
    dirty_.store(false, std::memory_order_relaxed);
}

std::string BgMatteStore::path() {
    std::lock_guard<std::mutex> lock(mutex_);
    return path_;
}

Status BgMatteStore::flush(const std::vector<u64>* keepKeys) {
    std::string target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        target = path_;
    }
    if (target.empty()) return OkStatus;
    return flush_to(target, keepKeys);
}

Status BgMatteStore::flush_to(const std::string& path, const std::vector<u64>* keepKeys) try {
    if (path.empty()) return Errc::InvalidArgument;
    bool samePath = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        samePath = path == path_;
        if (!samePath) {
            // Salvar como: carrega o do caminho antigo antes de trocar.
            ensure_loaded_locked();
            path_ = path;
            loaded_ = true;
            dirty_.store(true, std::memory_order_relaxed);
        }
    }
    if (keepKeys) retain(*keepKeys);
    if (!dirty()) return OkStatus;
    std::vector<u8> bytes;
    usize count = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [k, t] : tracks_) count += t.frames.size();
    }
    if (!count) {
        // Nada a guardar: um arquivo velho seria só lixo.
        (void)fileio::remove_file(path);
        dirty_.store(false, std::memory_order_relaxed);
        return OkStatus;
    }
    dirty_.store(false, std::memory_order_relaxed);   // o que mudar durante a escrita fica sujo
    encode(bytes);
    fileio::AtomicWriteOptions options;
    options.fsync = true;
    options.keepBackup = false;
    std::string error;
    const Status s = fileio::write_atomic(path, bytes.data(), bytes.size(), options, &error);
    if (!s.ok()) {
        dirty_.store(true, std::memory_order_relaxed);
        AUREA_LOG_WARN("Remover fundo: mascaras nao foram salvas (%s)", error.c_str());
    }
    return s;
} catch (const std::bad_alloc&) {
    dirty_.store(true, std::memory_order_relaxed);
    return Errc::OutOfMemory;
}

usize BgMatteStore::release_memory() {
    if (path().empty() || !flush().ok()) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (dirty_.load(std::memory_order_relaxed)) return 0;   // mudou durante a escrita
    const usize freed = bytes_;
    tracks_.clear();
    bytes_ = 0;
    loaded_ = false;
    return freed;
}

// --- BgRemoveService -------------------------------------------------------------
BgRemoveService::BgRemoveService(std::shared_ptr<BgMatteStore> store)
    : store_(store ? std::move(store) : make_bg_matte_store()) {}

BgRemoveService::~BgRemoveService() { clear(); }

void BgRemoveService::set_low_memory(bool low) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    refinedMax_ = low ? kLowMemoryRefinedMaxBytes : kRefinedMaxBytes;
}

BgRemoveService::MattePtr BgRemoveService::cached(u64 matteKey, i64 frame) try {
    auto out = std::make_shared<std::vector<f32>>();
    if (!store_->get(matteKey, frame, *out)) return nullptr;
    return out;
} catch (const std::bad_alloc&) {
    return nullptr;
}

BgRemoveService::RefinedPtr BgRemoveService::cached_refined(u64 matteKey, i64 frame, u64 refineKey) try {
    if (!refineKey) return nullptr;
    std::vector<u8> packed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = refined_.find(refined_slot_key(matteKey, frame));
        if (it == refined_.end() || it->second.refineKey != refineKey) return nullptr;
        it->second.stamp = ++stamp_;
        packed = it->second.packed;
    }
    auto out = std::make_shared<RotoRefined>();
    if (!roto_refined_unpack(packed, *out)) return nullptr;
    return out;
} catch (const std::bad_alloc&) {
    return nullptr;
}

void BgRemoveService::put_refined(u64 matteKey, i64 frame, u64 refineKey, const RotoRefined& refined) try {
    if (!refineKey) return;
    RefinedEntry e;
    e.refineKey = refineKey;
    roto_refined_pack(refined, e.packed);
    if (e.packed.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    e.stamp = ++stamp_;
    auto& slot = refined_[refined_slot_key(matteKey, frame)];
    refinedBytes_ -= std::min(refinedBytes_, slot.packed.size());
    refinedBytes_ += e.packed.size();
    slot = std::move(e);
    while (refinedBytes_ > refinedMax_ && refined_.size() > 1) {
        auto old = std::min_element(refined_.begin(), refined_.end(),
                                    [](const auto& a, const auto& b) { return a.second.stamp < b.second.stamp; });
        refinedBytes_ -= std::min(refinedBytes_, old->second.packed.size());
        refined_.erase(old);
    }
} catch (const std::bad_alloc&) {
}

BgRawPtr BgRemoveService::raw_from_native(u32 model, const RotoImage& image) try {
    const BgNativeSegmenter seg = gSegmenter.load(std::memory_order_acquire);
    if (!seg || !image.width || !image.height || image.rgb.size() < static_cast<usize>(image.width) * image.height * 3)
        return nullptr;
    auto raw = std::make_shared<BgRaw>();
    raw->prob.assign(kCells, 0.0f);
    if (!seg(gSegmenterCtx.load(std::memory_order_acquire), model, image.rgb.data(), image.width, image.height,
             kBgMatteSize, raw->prob.data()))
        return nullptr;
    for (f32& v : raw->prob) v = foreground_probability(v);
    roto_grid_rgb(image, kBgMatteSize, raw->rgb);
    return raw;
} catch (const std::bad_alloc&) {
    return nullptr;
}

BgRawPtr BgRemoveService::raw(const Source& source, i64 frame) try {
    const u64 key = raw_slot_key(source.matteKey, frame);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (RawSlot& s : raws_) if (s.raw && s.key == key) { s.stamp = ++stamp_; return s.raw; }
    }
    if (!source.fg || !source.factory) return nullptr;
    BgRawPtr out;
    if (source.model == kBgModelPerson) {
        if (RotoImagePtr image = source.fg->source_video_frame(source.factory, source.asset, source.rawKey, frame,
                                                               source.fps, kBgNativeSide))
            out = raw_from_native(source.model, *image);
        // O nativo falhou neste quadro (raro): o embutido segura o clipe.
        if (!out) AUREA_LOG_WARN("Remover fundo: segmentador nativo falhou no quadro %lld", static_cast<long long>(frame));
    }
    if (!out) {
        const DepthMapPtr map = source.fg->foreground_video_frame(source.factory, source.asset, source.rawKey, frame,
                                                                  source.fps);
        if (!map || map->disparity.size() != kCells || map->foregroundRgb.size() != kCells * 3) {
            lastFailure_.store(std::max<u32>(1u, source.fg->last_failure()), std::memory_order_relaxed);
            return nullptr;
        }
        auto r = std::make_shared<BgRaw>();
        r->prob = map->disparity;
        r->rgb = map->foregroundRgb;
        out = std::move(r);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (raws_.size() < kRawKept) raws_.push_back(RawSlot{key, out, ++stamp_});
    else {
        auto old = std::min_element(raws_.begin(), raws_.end(), [](const RawSlot& a, const RawSlot& b) { return a.stamp < b.stamp; });
        *old = RawSlot{key, out, ++stamp_};
    }
    return out;
} catch (const std::bad_alloc&) {
    lastFailure_.store(DepthMapService::kFailMemory, std::memory_order_relaxed);
    return nullptr;
}

BgRemoveService::Result BgRemoveService::compute_impl(const Source& source, i64 frame, u64 deadlineNs,
                                                      const std::atomic<u64>* generation, u64 expected) try {
    const i64 last = std::max<i64>(0, source.frameCount - 1);
    frame = std::clamp<i64>(frame, 0, last);
    auto stop = [&] {
        return (generation && generation->load(std::memory_order_relaxed) != expected)
            || (deadlineNs && monotonic_ns() > deadlineNs);
    };
    const bool refine = source.refine.on();
    const u64 refineKey = refine ? source.refine.hash() : 0;
    MattePtr matte = cached(source.matteKey, frame);
    RefinedPtr refined = refine && matte ? cached_refined(source.matteKey, frame, refineKey) : nullptr;
    if (matte && (!refine || refined)) return {matte, refined};
    if (!matte) {
        const BgRawPtr cur = raw(source, frame);
        if (!cur) return {};
        if (stop()) return {};
        // Vizinhos: a janela é simétrica e fixa. Vizinho que não lê no meio do
        // clipe = falha (nunca um recorte que dependa da ordem de cálculo).
        const BgRawPtr prev = frame > 0 ? raw(source, frame - 1) : nullptr;
        if (frame > 0 && !prev) return {};
        if (stop()) return {};
        const BgRawPtr next = frame < last ? raw(source, frame + 1) : nullptr;
        if (frame < last && !next) return {};
        auto m = std::make_shared<std::vector<f32>>();
        bg_matte(prev.get(), *cur, next.get(), *m);
        store_->put(source.matteKey, frame, *m);
        computed_.fetch_add(1, std::memory_order_relaxed);
        matte = std::move(m);
    }
    if (refine && !refined && !stop() && source.fg && source.factory) {
        // Borda fina: a faixa da borda na resolução do quadro. Sem
        // trepidação encadeada (cada quadro sozinho): seek e export batem.
        if (RotoImagePtr image = source.fg->source_video_frame(source.factory, source.asset, source.rawKey, frame,
                                                               source.fps, source.refineMaxSide)) {
            RotoRefineSettings s = source.refine;
            s.chatter = 0.0f;
            const f32 sourceSide = std::max(source.layerW, source.layerH);
            if (sourceSide > 1.0f) s.radius *= static_cast<f32>(std::max(image->width, image->height)) / sourceSide;
            s.radius = std::max(s.radius, 0.5f);
            auto out = std::make_shared<RotoRefined>();
            roto_refine(matte->data(), kSize, *image, s, nullptr, *out);
            put_refined(source.matteKey, frame, refineKey, *out);
            refined = std::move(out);
        }
        // Quadro ilegível na resolução cheia: fica a grade (o export não trava).
    }
    return {matte, refined};
} catch (const std::bad_alloc&) {
    AUREA_LOG_WARN("Remover fundo: memoria insuficiente");
    lastFailure_.store(DepthMapService::kFailMemory, std::memory_order_relaxed);
    return {};
}

BgRemoveService::Result BgRemoveService::compute(const Source& source, i64 frame, u64 deadlineNs) {
    return compute_impl(source, frame, deadlineNs, nullptr, 0);
}

void BgRemoveService::ensure_thread() {
    if (running_) return;
    thread_ = std::thread([this] { thread_main(); });
    running_ = true;
}

void BgRemoveService::request(const Source& source, i64 frame) try {
    std::lock_guard<std::mutex> lock(queueMutex_);
    auto job = std::make_unique<Job>();
    job->source = source;
    job->first = job->last = frame;
    // Durante a análise o quadro que a pessoa olha vai na frente.
    focus_ = std::move(job);
    ensure_thread();
    wake_.notify_one();
} catch (const std::system_error&) {
    AUREA_LOG_WARN("Remover fundo: worker indisponivel");
} catch (const std::bad_alloc&) {
}

void BgRemoveService::analyze(const Source& source, i64 first, i64 last) try {
    std::lock_guard<std::mutex> lock(queueMutex_);
    generation_.fetch_add(1, std::memory_order_relaxed);   // a análise anterior para
    auto job = std::make_unique<Job>();
    job->source = source;
    // A análise guarda o recorte (o que vai para o arquivo). A borda fina é
    // refeita sob demanda (só em memória).
    job->source.refine = RotoRefineSettings{};
    job->first = std::max<i64>(0, std::min(first, last));
    job->last = std::max<i64>(0, std::max(first, last));
    job->range = true;
    progress_ = Progress{source.matteKey, 0, job->last - job->first + 1, true, false, 0};
    pending_ = std::move(job);
    ensure_thread();
    wake_.notify_one();
} catch (const std::system_error&) {
    AUREA_LOG_WARN("Remover fundo: worker indisponivel");
} catch (const std::bad_alloc&) {
}

BgRemoveService::Progress BgRemoveService::progress() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return progress_;
}

void BgRemoveService::cancel() noexcept {
    std::lock_guard<std::mutex> lock(queueMutex_);
    generation_.fetch_add(1, std::memory_order_relaxed);
    pending_.reset();
    focus_.reset();
    progress_.running = false;
}

void BgRemoveService::set_ready_callback(void (*fn)(void*), void* ctx) noexcept {
    std::lock_guard<std::mutex> lock(queueMutex_);
    readyFn_ = fn;
    readyCtx_ = ctx;
}

void BgRemoveService::notify() {
    void (*fn)(void*) = nullptr;
    void* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        fn = readyFn_;
        ctx = readyCtx_;
    }
    if (fn) fn(ctx);
}

void BgRemoveService::thread_main() noexcept {
    set_current_thread_name("aurea-bgremove");
    set_current_thread_priority(ThreadPriority::Background);
    for (;;) {
        std::unique_ptr<Job> job;
        u64 gen = 0;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            wake_.wait(lock, [&] { return !running_ || pending_ || focus_; });
            if (!running_) break;
            // O quadro da prévia antes da análise.
            job = focus_ ? std::move(focus_) : std::move(pending_);
            gen = generation_.load(std::memory_order_relaxed);
        }
        if (!job->range) {
            (void)compute_impl(job->source, job->first, 0, nullptr, 0);
            notify();
            continue;
        }
        lastFailure_.store(0, std::memory_order_relaxed);
        bool failed = false;
        i64 done = 0;
        for (i64 f = job->first; f <= job->last; ++f) {
            if (generation_.load(std::memory_order_relaxed) != gen) break;
            // Entre quadros, o quadro que a pessoa olha vai na frente.
            std::unique_ptr<Job> focus;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                focus = std::move(focus_);
            }
            if (focus) { (void)compute_impl(focus->source, focus->first, 0, nullptr, 0); notify(); }
            // Um quadro que falha não para o resto do clipe; o status avisa.
            if (!compute_impl(job->source, f, 0, &generation_, gen).matte
                && generation_.load(std::memory_order_relaxed) == gen)
                failed = true;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (generation_.load(std::memory_order_relaxed) == gen) progress_.done = ++done;
            }
            notify();
        }
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (generation_.load(std::memory_order_relaxed) == gen) {
                progress_.running = false;
                progress_.failed = failed;
                progress_.reason = failed ? std::max<u32>(1u, lastFailure_.load(std::memory_order_relaxed)) : 0u;
            }
        }
        // O que foi analisado vai para o arquivo do projeto (sobrevive a
        // fechar o app sem salvar). Também depois de cancelar.
        (void)store_->flush();
        notify();
    }
}

void BgRemoveService::trim() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    refined_.clear();
    refinedBytes_ = 0;
    raws_.clear();
}

void BgRemoveService::clear() noexcept {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        running_ = false;
        pending_.reset();
        focus_.reset();
        generation_.fetch_add(1, std::memory_order_relaxed);
        progress_ = Progress{};
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    refined_.clear();
    refinedBytes_ = 0;
    raws_.clear();
}

} // namespace aurea::ai
