// =============================================================================
//  Aurea / render / PreRenderPlan.cpp — contas do painel Pre Render & Pre Export.
// =============================================================================
#include "aurea/render/PreRenderPlan.hpp"
#include "aurea/export/BitratePolicy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace aurea::prerender {
namespace {
u32 even_side(f64 v) noexcept {
    const i64 n = static_cast<i64>(std::lround(v / 2.0)) * 2;
    return static_cast<u32>(std::clamp<i64>(n, 16, 16384));
}
u64 mix(u64 a, u64 b) noexcept {
    // splitmix64 sobre a soma: hashes diferentes para cada (trecho, edição).
    u64 z = a + 0x9e3779b97f4a7c15ull + (b << 6) + (b >> 2);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
constexpr i64 kMaxSegments = 4'000'000;   // ~9 h a 120 qps: acima disso é lixo
} // namespace

ScaledSize scaled_size(u32 compWidth, u32 compHeight, u32 divisor) noexcept {
    if (!compWidth || !compHeight) return {};
    const f64 d = static_cast<f64>(normalize_divisor(divisor));
    return {even_side(compWidth / d), even_side(compHeight / d)};
}

u64 cache_frame_bytes(u32 width, u32 height, f64 measuredBytesPerPixel) noexcept {
    const f64 bpp = measuredBytesPerPixel > 0.05 && measuredBytesPerPixel <= 9.0 ? measuredBytesPerPixel
                                                                               : kDefaultCacheBytesPerPixel;
    return static_cast<u64>(std::ceil(static_cast<f64>(width) * height * bpp)) + kCacheFrameOverheadBytes;
}

u64 video_bytes_per_second(u32 width, u32 height, f64 fps) noexcept {
    const u32 videoBps = export_video_bitrate_bps(width, height, fps > 0.0 ? fps : 30.0, ExportCodec::H264,
                                                  ExportQuality::High);
    return export_estimated_bytes(videoBps, kExportAudioKbps * 1000u, 1.0);
}

u64 available_bytes(const Budget& b) noexcept {
    u64 room = b.capBytes;
    if (b.freeDiskKnown) {
        // O trabalho anterior sai antes do novo começar: o livre conta com ele.
        const u64 free = b.freeDiskBytes > std::numeric_limits<u64>::max() - b.reclaimableBytes
            ? std::numeric_limits<u64>::max() : b.freeDiskBytes + b.reclaimableBytes;
        const u64 usable = free > b.reserveBytes ? (free - b.reserveBytes) / 2 : 0;
        room = std::min(room, usable);
    }
    return room;
}

f64 fits_seconds(u64 availableBytes, u64 bytesPerSecond) noexcept {
    if (!bytesPerSecond) return 0.0;
    return static_cast<f64>(availableBytes) / static_cast<f64>(bytesPerSecond);
}

Fit plan_fit(u32 compWidth, u32 compHeight, f64 fps, u32 divisor, Mode mode, const Budget& budget,
             i64 rangeFrames, f64 measuredBytesPerPixel) noexcept {
    Fit f;
    f.divisor = normalize_divisor(divisor);
    const ScaledSize size = scaled_size(compWidth, compHeight, f.divisor);
    f.width = size.width;
    f.height = size.height;
    f.fps = fps > 0.0 && std::isfinite(fps) ? fps : 30.0;
    if (!f.width || !f.height) return f;
    f.bytesPerSecond = mode == Mode::PreExport
        ? video_bytes_per_second(f.width, f.height, f.fps)
        : static_cast<u64>(std::ceil(static_cast<f64>(cache_frame_bytes(f.width, f.height, measuredBytesPerPixel)) * f.fps));
    f.availableBytes = available_bytes(budget);
    f.fitsSeconds = fits_seconds(f.availableBytes, f.bytesPerSecond);
    f.rangeSeconds = rangeFrames > 0 ? static_cast<f64>(rangeFrames) / f.fps : 0.0;
    f.rangeBytes = static_cast<u64>(std::ceil(f.rangeSeconds * static_cast<f64>(f.bytesPerSecond)));
    f.fits = f.rangeSeconds > 0.0 && f.rangeBytes <= f.availableBytes;
    return f;
}

// --- EditLedger --------------------------------------------------------------
void EditLedger::reset() noexcept {
    ++epoch_;
    stamps_.clear();
}

void EditLedger::touch_all() noexcept {
    ++epoch_;
}

void EditLedger::touch(i64 from, i64 to) noexcept {
    from = std::max<i64>(0, from);
    if (to <= from) return;
    const i64 first = from / kSegmentFrames;
    const i64 last = std::min<i64>((to - 1) / kSegmentFrames, kMaxSegments - 1);
    if (first > last) return;
    // Sem exceções no Android/iOS (-fno-exceptions): o livro é limitado a
    // kMaxSegments, então o resize é pequeno e não precisa de try.
    if (static_cast<i64>(stamps_.size()) <= last) stamps_.resize(static_cast<usize>(last + 1), 0);
    ++serial_;
    for (i64 s = first; s <= last; ++s) stamps_[static_cast<usize>(s)] = mix(stamps_[static_cast<usize>(s)], serial_);
}

u64 EditLedger::segment_hash(i64 segment) const noexcept {
    const u64 own = segment >= 0 && segment < static_cast<i64>(stamps_.size()) ? stamps_[static_cast<usize>(segment)] : 0;
    return mix(epoch_, own);
}

EditLedger::Snapshot EditLedger::capture(i64 in, i64 out) const {
    Snapshot s;
    in = std::max<i64>(0, in);
    if (out <= in) return s;
    s.in = in;
    s.out = out;
    s.epoch = epoch_;
    const i64 first = in / kSegmentFrames, last = (out - 1) / kSegmentFrames;
    s.segments.reserve(static_cast<usize>(last - first + 1));
    for (i64 seg = first; seg <= last; ++seg) s.segments.push_back(segment_hash(seg));
    return s;
}

bool EditLedger::frame_clean(const Snapshot& s, i64 frame) const noexcept {
    if (s.empty() || frame < s.in || frame >= s.out) return false;
    const i64 seg = frame / kSegmentFrames;
    const i64 index = seg - s.in / kSegmentFrames;
    if (index < 0 || index >= static_cast<i64>(s.segments.size())) return false;
    return s.segments[static_cast<usize>(index)] == segment_hash(seg);
}

u32 EditLedger::dirty_segments(const Snapshot& s) const noexcept {
    if (s.empty()) return 0;
    const i64 first = s.in / kSegmentFrames;
    u32 dirty = 0;
    for (usize i = 0; i < s.segments.size(); ++i)
        if (s.segments[i] != segment_hash(first + static_cast<i64>(i))) ++dirty;
    return dirty;
}

bool EditLedger::any_clean(const Snapshot& s) const noexcept {
    if (s.empty()) return false;
    const i64 first = s.in / kSegmentFrames;
    for (usize i = 0; i < s.segments.size(); ++i)
        if (s.segments[i] == segment_hash(first + static_cast<i64>(i))) return true;
    return false;
}

std::vector<std::pair<i64, i64>> EditLedger::dirty_ranges(const Snapshot& s) const {
    std::vector<std::pair<i64, i64>> out;
    if (s.empty()) return out;
    const i64 first = s.in / kSegmentFrames;
    for (usize i = 0; i < s.segments.size(); ++i) {
        const i64 seg = first + static_cast<i64>(i);
        if (s.segments[i] == segment_hash(seg)) continue;
        const i64 a = std::max(s.in, seg * kSegmentFrames), b = std::min(s.out, (seg + 1) * kSegmentFrames);
        if (!out.empty() && out.back().second == a) out.back().second = b;
        else out.emplace_back(a, b);
    }
    return out;
}

// --- Trecho --------------------------------------------------------------------
RangeChoice resolve_range(i64 selectedIn, i64 selectedOut, bool hasWorkArea, i64 workIn, i64 workOut,
                          i64 contentEnd, i64 duration) noexcept {
    RangeChoice r;
    duration = std::max<i64>(1, duration);
    const i64 end = std::clamp<i64>(contentEnd > 0 ? contentEnd : duration, 1, duration);
    if (selectedIn >= 0 && selectedOut > selectedIn) {
        r.in = std::clamp<i64>(selectedIn, 0, duration - 1);
        r.out = std::clamp<i64>(selectedOut, r.in + 1, duration);
        r.explicitRange = true;
        return r;
    }
    if (hasWorkArea && workOut > workIn) {
        r.out = std::clamp<i64>(workOut, 1, duration);
        r.in = std::clamp<i64>(workIn, 0, r.out - 1);
        return r;
    }
    r.in = 0;
    r.out = end;
    return r;
}

std::pair<i64, i64> range_with_start(i64 playhead, i64 in, i64 out, i64 contentEnd, i64 duration) noexcept {
    (void)in;
    duration = std::max<i64>(1, duration);
    const i64 start = std::clamp<i64>(playhead, 0, duration - 1);
    i64 end = out;
    if (end <= start) end = std::clamp<i64>(contentEnd > start ? contentEnd : duration, start + 1, duration);
    return {start, std::clamp<i64>(end, start + 1, duration)};
}

std::pair<i64, i64> range_with_end(i64 playhead, i64 in, i64 out, i64 duration) noexcept {
    (void)out;
    duration = std::max<i64>(1, duration);
    const i64 end = std::clamp<i64>(playhead + 1, 1, duration);
    i64 start = in;
    if (start < 0 || start >= end) start = 0;
    return {start, end};
}

} // namespace aurea::prerender
