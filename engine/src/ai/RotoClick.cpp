#include "aurea/ai/RotoClick.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"

#include <datareader.h>
#include <net.h>

#include <algorithm>
#include <cmath>
#include <new>

namespace aurea::ai {
namespace embedded {
const unsigned char* roto_click_encoder_weights();
const char* roto_click_encoder_param();
const unsigned char* roto_click_decoder_weights();
const char* roto_click_decoder_param();
const unsigned char* roto_click_prompt();
}
namespace {
constexpr u32 kGrid = 32;       // saída do encoder: 256 × 32 × 32
constexpr u32 kChannels = 256;
constexpr u32 kLow = 128;       // máscaras do decoder
constexpr u32 kCandidates = 4;  // 0 = máscara única; 1..3 = uma por escala
// Dois candidatos quase empatados: o maior. Um toque costuma querer o objeto
// inteiro, e sem a margem o ruído numérico decide entre a parte e o todo.
constexpr f32 kTieMargin = 0.01f;

// Layout de efficienttam-ti-prompt.bin (f32): matriz gaussiana 2×128, tabela de
// rótulos 5×256 ([-1, 0, 1, 2, 3]), "sem máscara" 256 e "sem memória" 256.
constexpr usize kTableAt = 2 * 128;
constexpr usize kNoMaskAt = kTableAt + 5 * kChannels;
constexpr usize kNoMemoryAt = kNoMaskAt + kChannels;
constexpr u32 kLabelPadding = 0, kLabelBackground = 1, kLabelObject = 2;

const f32* prompt_table() noexcept { return reinterpret_cast<const f32*>(embedded::roto_click_prompt()); }

class Budget final : public ncnn::Allocator {
public:
    void* fastMalloc(size_t size) override {
        constexpr size_t budget = 192u * 1024u * 1024u;
        if (size > budget - 64 || live_ > budget - size - 64) {
            AUREA_LOG_WARN("RotoClick allocation budget: live=%zu request=%zu", live_, size);
            return nullptr;
        }
        auto* base = static_cast<unsigned char*>(ncnn::fastMalloc(size + 64));
        if (!base) return nullptr;
        *reinterpret_cast<size_t*>(base) = size + 64;
        live_ += size + 64;
        peak_ = std::max(peak_, live_);
        return base + 64;
    }
    void fastFree(void* ptr) override {
        if (!ptr) return;
        auto* base = static_cast<unsigned char*>(ptr) - 64;
        live_ -= *reinterpret_cast<size_t*>(base);
        ncnn::fastFree(base);
    }
    [[nodiscard]] size_t peak() const noexcept { return peak_; }
private:
    size_t live_ = 0, peak_ = 0;
};

bool load(ncnn::Net& net, const char* param, const unsigned char* weights) {
    if (!param || !weights) return false;
    net.opt.num_threads = 1;
    net.opt.use_vulkan_compute = false;
    net.opt.lightmode = true;
    net.opt.use_fp16_storage = false;
    net.opt.use_fp16_arithmetic = false;
    net.opt.use_bf16_storage = false;
    net.opt.use_winograd_convolution = false;
    net.opt.use_sgemm_convolution = false;
    ncnn::DataReaderFromMemory reader(weights);
    return net.load_param_mem(param) == 0 && net.load_model(reader) == 0;
}

/// Pontos da rede: cada clique e a marca final de "sem caixa".
void sparse_tokens(const RotoClick::Point* points, u32 count, ncnn::Mat& out) {
    const f32* c = prompt_table();
    const f32* table = c + kTableAt;
    for (u32 i = 0; i <= count; ++i) {
        const bool padding = i == count;
        const f32 half = 0.5f / RotoClick::kInput;
        const f32 x = padding ? half : std::clamp(points[i].x + half, 0.0f, 1.0f);
        const f32 y = padding ? half : std::clamp(points[i].y + half, 0.0f, 1.0f);
        const u32 label = padding ? kLabelPadding : points[i].positive ? kLabelObject : kLabelBackground;
        const f32* row = table + static_cast<usize>(label) * kChannels;
        f32* token = out.row(static_cast<int>(i));
        for (u32 k = 0; k < 128; ++k) {
            const f32 v = (x * 2.0f - 1.0f) * c[k] + (y * 2.0f - 1.0f) * c[128 + k];
            token[k] = (padding ? 0.0f : std::sin(v)) + row[k];
            token[128 + k] = (padding ? 0.0f : std::cos(v)) + row[128 + k];
        }
    }
}

/// Qual candidato vale. Com um clique só: a rede dá três escalas; fica a de nota
/// melhor que contém o clique. Com vários, a máscara única (como no SAM).
u32 pick_candidate(const ncnn::Mat& masks, const f32* scores, const RotoClick::Point* points, u32 count) {
    if (count > 1) return 0;
    const u32 px = std::min(kLow - 1, static_cast<u32>(std::clamp(points[0].x, 0.0f, 1.0f) * kLow));
    const u32 py = std::min(kLow - 1, static_cast<u32>(std::clamp(points[0].y, 0.0f, 1.0f) * kLow));
    auto holds = [&](u32 m) { return masks.channel(static_cast<int>(m)).row(static_cast<int>(py))[px] > 0.0f; };
    f32 best = -1e30f;
    for (u32 m = 1; m < kCandidates; ++m) if (holds(m)) best = std::max(best, scores[m]);
    const bool any = best > -1e29f;
    if (!any) for (u32 m = 1; m < kCandidates; ++m) best = std::max(best, scores[m]);
    u32 chosen = 1;
    usize largest = 0;
    for (u32 m = 1; m < kCandidates; ++m) {
        if (scores[m] < best - kTieMargin || (any && !holds(m))) continue;
        const f32* p = masks.channel(static_cast<int>(m));
        usize area = 0;
        for (u32 i = 0; i < kLow * kLow; ++i) area += p[i] > 0.0f;
        if (area >= largest) { largest = area; chosen = m; }
    }
    return chosen;
}

/// Logits 128² → probabilidade size² (bilinear, centros de pixel).
void upsample(const f32* logits, u32 size, std::vector<f32>& out) {
    out.resize(static_cast<usize>(size) * size);
    const f32 scale = static_cast<f32>(kLow) / static_cast<f32>(size);
    for (u32 y = 0; y < size; ++y) {
        const f32 fy = std::clamp((y + 0.5f) * scale - 0.5f, 0.0f, static_cast<f32>(kLow - 1));
        const u32 y0 = static_cast<u32>(fy), y1 = std::min(y0 + 1, kLow - 1);
        const f32 ty = fy - static_cast<f32>(y0);
        for (u32 x = 0; x < size; ++x) {
            const f32 fx = std::clamp((x + 0.5f) * scale - 0.5f, 0.0f, static_cast<f32>(kLow - 1));
            const u32 x0 = static_cast<u32>(fx), x1 = std::min(x0 + 1, kLow - 1);
            const f32 tx = fx - static_cast<f32>(x0);
            const f32 top = logits[y0 * kLow + x0] * (1.0f - tx) + logits[y0 * kLow + x1] * tx;
            const f32 low = logits[y1 * kLow + x0] * (1.0f - tx) + logits[y1 * kLow + x1] * tx;
            const f32 v = top * (1.0f - ty) + low * ty;
            out[static_cast<usize>(y) * size + x] = 1.0f / (1.0f + std::exp(-v));
        }
    }
}
} // namespace

bool RotoClick::encode(const u8* rgb) {
    ncnn::Net net;
    if (!load(net, embedded::roto_click_encoder_param(), embedded::roto_click_encoder_weights())) {
        AUREA_LOG_WARN("RotoClick: encoder could not be loaded");
        return false;
    }
    Budget allocator;
    const f32 mean[3] = {0.485f, 0.456f, 0.406f};
    const f32 inv[3] = {1.0f / 0.229f, 1.0f / 0.224f, 1.0f / 0.225f};
    const auto start = monotonic_ns();
    ncnn::Mat input(static_cast<int>(kInput), static_cast<int>(kInput), 3, sizeof(f32), &allocator);
    if (input.empty()) return false;
    for (int c = 0; c < 3; ++c) {
        f32* plane = input.channel(c);
        for (u32 i = 0; i < kInput * kInput; ++i) plane[i] = (rgb[static_cast<usize>(i) * 3 + static_cast<usize>(c)] * (1.0f / 255.0f) - mean[c]) * inv[c];
    }
    ncnn::Extractor ex = net.create_extractor();
    ex.set_blob_allocator(&allocator);
    ex.set_workspace_allocator(&allocator);
    ncnn::Mat out;
    const int a = ex.input("in0", input);
    const int b = a ? a : ex.extract("out0", out);
    if (b || out.empty() || out.w != static_cast<int>(kGrid) || out.h != static_cast<int>(kGrid) || out.c != static_cast<int>(kChannels)) {
        AUREA_LOG_WARN("RotoClick encoder input=%d output=%d dims=%d,%d,%d", a, b, out.w, out.h, out.c);
        return false;
    }
    features_.resize(static_cast<usize>(kChannels) * kGrid * kGrid);
    for (u32 c = 0; c < kChannels; ++c) {
        const f32* plane = out.channel(static_cast<int>(c));
        std::copy(plane, plane + kGrid * kGrid, features_.begin() + static_cast<std::ptrdiff_t>(static_cast<usize>(c) * kGrid * kGrid));
    }
    AUREA_LOG_INFO("RotoClick encoder %.1f ms peak %.1f MiB", (monotonic_ns() - start) * 1e-6, allocator.peak() / 1048576.0);
    return true;
}

bool RotoClick::decode(const Point* points, u32 count, u32 size, std::vector<f32>& out) {
    ncnn::Net net;
    if (!load(net, embedded::roto_click_decoder_param(), embedded::roto_click_decoder_weights())) {
        AUREA_LOG_WARN("RotoClick: decoder could not be loaded");
        return false;
    }
    Budget allocator;
    const auto start = monotonic_ns();
    const f32* c = prompt_table();
    const int g = static_cast<int>(kGrid), n = static_cast<int>(kChannels);
    ncnn::Mat pixels(g, g, n, sizeof(f32), &allocator);
    ncnn::Mat dense(g, g, n, sizeof(f32), &allocator);
    ncnn::Mat sparse(n, static_cast<int>(count + 1), sizeof(f32), &allocator);
    if (pixels.empty() || dense.empty() || sparse.empty()) return false;
    for (u32 k = 0; k < kChannels; ++k) {
        f32* p = pixels.channel(static_cast<int>(k));
        f32* d = dense.channel(static_cast<int>(k));
        const f32* f = features_.data() + static_cast<usize>(k) * kGrid * kGrid;
        const f32 noMemory = c[kNoMemoryAt + k], noMask = c[kNoMaskAt + k];
        for (u32 i = 0; i < kGrid * kGrid; ++i) { p[i] = f[i] + noMemory; d[i] = noMask; }
    }
    sparse_tokens(points, count, sparse);
    ncnn::Extractor ex = net.create_extractor();
    ex.set_blob_allocator(&allocator);
    ex.set_workspace_allocator(&allocator);
    ncnn::Mat masks, scores, objectScore;
    int r = ex.input("in0", pixels);
    if (!r) r = ex.input("in1", dense);
    if (!r) r = ex.input("in2", sparse);
    if (!r) r = ex.extract("out0", masks);
    if (!r) r = ex.extract("out1", scores);
    if (!r) r = ex.extract("out3", objectScore);
    if (r || masks.w != static_cast<int>(kLow) || masks.h != static_cast<int>(kLow) || masks.c != static_cast<int>(kCandidates)
        || scores.total() < kCandidates || objectScore.empty()) {
        AUREA_LOG_WARN("RotoClick decoder status=%d masks=%d,%d,%d scores=%zu", r, masks.w, masks.h, masks.c, scores.total());
        return false;
    }
    const f32 object = static_cast<const f32*>(objectScore.data)[0];
    if (!(object > 0.0f)) {
        AUREA_LOG_INFO("RotoClick: no object at the click (score %.2f)", object);
        return false;
    }
    const f32* candidates = static_cast<const f32*>(scores.data);
    const u32 chosen = pick_candidate(masks, candidates, points, count);
    upsample(masks.channel(static_cast<int>(chosen)), size, out);
    AUREA_LOG_INFO("RotoClick decoder %.1f ms peak %.1f MiB mask %u score %.3f", (monotonic_ns() - start) * 1e-6,
                   allocator.peak() / 1048576.0, chosen, candidates[chosen]);
    return true;
}

bool RotoClick::run(u64 imageKey, const u8* rgb, const Point* points, u32 count, u32 size, std::vector<f32>& out) try {
    if (!rgb || !points || !count || !size || !embedded::roto_click_prompt()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (count > kMaxPoints) { points += count - kMaxPoints; count = kMaxPoints; }
    if (featuresKey_ != imageKey || features_.empty()) {
        featuresKey_ = 0;
        if (!encode(rgb)) { features_.clear(); return false; }
        featuresKey_ = imageKey;
    }
    return decode(points, count, size, out);
} catch (const std::bad_alloc&) {
    AUREA_LOG_WARN("RotoClick: out of memory");
    return false;
}

void RotoClick::clear() noexcept {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return;
    featuresKey_ = 0;
    std::vector<f32>().swap(features_);
}

} // namespace aurea::ai
