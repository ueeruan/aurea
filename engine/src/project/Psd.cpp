#include "aurea/project/Psd.hpp"
#include "stb_image.h"
#include <algorithm>
#include <limits>

namespace aurea::psd {
namespace {
struct Reader {
    std::span<const u8> data; usize p = 0; bool ok = true;
    std::span<const u8> take(usize n) {
        if (!ok || n > data.size() - p) { ok = false; return {}; }
        auto s = data.subspan(p, n); p += n; return s;
    }
    u8 byte() { auto s = take(1); return s.empty() ? 0 : s[0]; }
    u16 word() { const u16 a = byte(); return static_cast<u16>((a << 8) | byte()); }
    u32 dword() { const u32 a = word(); return (a << 16) | word(); }
    i32 integer() { return static_cast<i32>(dword()); }
    std::string tag() { auto s = take(4); return {s.begin(), s.end()}; }
    Reader block() { const auto n = dword(); return Reader{take(n)}; }
};
struct Channel { i16 id; u32 length; };
struct Record {
    Layer layer; std::vector<Channel> channels;
    u32 section = 0; bool clipping = false;
    i32 mx = 0, my = 0; u32 mw = 0, mh = 0;
    u8 maskDefault = 255, maskFlags = 2;
};
bool dimensions(i32 a, i32 b, u32& size) {
    const i64 d = static_cast<i64>(b) - a;
    if (d < 0 || d > 16384) return false;
    size = static_cast<u32>(d); return true;
}
void utf8(std::string& s, u32 c) {
    if (c < 128) s += static_cast<char>(c);
    else if (c < 2048) { s += static_cast<char>(192 | (c >> 6)); s += static_cast<char>(128 | (c & 63)); }
    else if (c < 65536) { s += static_cast<char>(224 | (c >> 12)); s += static_cast<char>(128 | ((c >> 6) & 63)); s += static_cast<char>(128 | (c & 63)); }
    else { s += static_cast<char>(240 | (c >> 18)); s += static_cast<char>(128 | ((c >> 12) & 63)); s += static_cast<char>(128 | ((c >> 6) & 63)); s += static_cast<char>(128 | (c & 63)); }
}
BlendMode blend(const std::string& key, bool& approximate) {
    constexpr const char* keys[] = {"norm","lddg","fsub","mul ","scrn","over","dark","lite","div ","idiv","hLit","sLit","diff","smud","hue ","sat ","colr","lum ","fdiv","vLit","lddg","lbrn"};
    for (u32 i = 0; i < std::size(keys); ++i) if (key == keys[i]) return static_cast<BlendMode>(i);
    approximate = true; return BlendMode::Normal;
}
bool channel(Reader r, u32 w, u32 h, u16 depth, std::vector<u8>& out) {
    const usize row = static_cast<usize>(w) * (depth / 8), count = row * h;
    if (count > kMaxDecodedBytes || r.data.size() < 2) return false;
    const auto compression = r.word(); out.assign(count, 0);
    if (compression == 0) {
        auto s = r.take(count); if (!r.ok) return false; std::copy(s.begin(), s.end(), out.begin());
    } else if (compression == 1) {
        std::vector<u16> sizes(h); for (auto& n : sizes) n = r.word();
        if (!r.ok) return false;
        for (u32 y = 0; y < h; ++y) {
            Reader rowData{r.take(sizes[y])}; usize x = 0;
            while (rowData.ok && rowData.p < rowData.data.size()) {
                const i32 n = static_cast<i8>(rowData.byte());
                if (n == -128) continue;
                const usize length = static_cast<usize>(n < 0 ? 1 - n : n + 1);
                if (length > row - x) return false;
                if (n < 0) { const auto v = rowData.byte(); std::fill_n(out.begin() + y * row + x, length, v); }
                else { auto s = rowData.take(length); std::copy(s.begin(), s.end(), out.begin() + y * row + x); }
                x += length;
            }
            if (!r.ok || !rowData.ok || x != row) return false;
        }
    } else if (compression == 2 || compression == 3) {
        auto s = r.take(r.data.size() - r.p);
        if (stbi_zlib_decode_buffer(reinterpret_cast<char*>(out.data()), static_cast<int>(count),
                reinterpret_cast<const char*>(s.data()), static_cast<int>(s.size())) != static_cast<int>(count)) return false;
        if (compression == 3) for (u32 y = 0; y < h; ++y) {
            auto* d = out.data() + y * row;
            if (depth == 8) for (u32 x = 1; x < w; ++x) d[x] = static_cast<u8>(d[x] + d[x - 1]);
            else for (u32 x = 1; x < w; ++x) {
                const u16 v = static_cast<u16>((d[2*x] << 8 | d[2*x+1]) + (d[2*x-2] << 8 | d[2*x-1]));
                d[2*x] = static_cast<u8>(v >> 8); d[2*x+1] = static_cast<u8>(v);
            }
        }
    } else return false;
    return r.ok;
}
}

Status read(std::span<const u8> bytes, Document& out) {
    if (bytes.size() > kMaxFileBytes) return Errc::BudgetExceeded;
    Reader r{bytes}; Document doc;
    if (r.tag() != "8BPS" || r.word() != 1) return Errc::UnsupportedFormat;
    r.take(6); const auto channels = r.word(); doc.height = r.dword(); doc.width = r.dword();
    const auto depth = r.word(), mode = r.word();
    if (!r.ok || channels < 1 || channels > 56 || !doc.width || !doc.height || doc.width > 16384 || doc.height > 16384) return Errc::CorruptData;
    if ((depth != 8 && depth != 16) || (mode != 1 && mode != 3)) return Errc::UnsupportedFormat;
    r.block(); r.block(); auto lm = r.block(); auto info = lm.block();
    // Photoshop stores 16-bit layer records in Lr16 after the global mask.
    if (lm.ok && lm.data.size() - lm.p >= 4) {
        lm.block();
        while (lm.ok && lm.data.size() - lm.p >= 12) {
            if (lm.tag() != "8BIM") return Errc::UnsupportedFormat;
            const auto key = lm.tag(); const auto length = lm.dword();
            Reader tagged{lm.take(length)}; lm.take(length % 2);
            if (key == "Lr16" && depth == 16) info = tagged;
        }
    }
    const i32 signedCount = static_cast<i16>(info.word()); const u32 count = static_cast<u32>(std::abs(signedCount));
    if (!r.ok || !lm.ok || !info.ok || count > 2048) return Errc::CorruptData;
    if (!count) return Errc::UnsupportedFormat;
    std::vector<Record> records(count); usize budget = 0;
    for (auto& rec : records) {
        auto& l = rec.layer; l.top = info.integer(); l.left = info.integer();
        const auto bottom = info.integer(), right = info.integer();
        if (!dimensions(l.left, right, l.width) || !dimensions(l.top, bottom, l.height)) return Errc::BudgetExceeded;
        const auto n = info.word(); if (n > 56) return Errc::CorruptData;
        for (u32 c = 0; c < n; ++c) { const auto id = static_cast<i16>(info.word()); const auto len = info.dword(); rec.channels.push_back({id,len}); }
        if (info.tag() != "8BIM") return Errc::CorruptData;
        l.blend = blend(info.tag(), doc.approximated); l.opacity = info.byte() / 255.f;
        rec.clipping = info.byte() != 0; l.visible = !(info.byte() & 2); info.byte();
        auto extra = info.block(); auto mask = extra.block();
        if (!mask.data.empty()) {
            rec.my = mask.integer(); rec.mx = mask.integer(); const auto mb = mask.integer(), mr = mask.integer();
            if (!dimensions(rec.mx, mr, rec.mw) || !dimensions(rec.my, mb, rec.mh)) return Errc::CorruptData;
            rec.maskDefault = mask.byte(); rec.maskFlags = mask.byte(); if (!mask.ok) return Errc::CorruptData;
            if (rec.maskFlags & 1) { rec.mx += l.left; rec.my += l.top; }
        }
        extra.block(); const usize nameStart = extra.p; const auto nameLen = extra.byte();
        auto name = extra.take(nameLen); l.name.assign(name.begin(), name.end()); extra.take((4 - (extra.p - nameStart) % 4) % 4);
        while (extra.ok && extra.data.size() - extra.p >= 12) {
            if (extra.tag() != "8BIM") return Errc::CorruptData;
            const auto key = extra.tag(); const auto len = extra.dword(); Reader item{extra.take(len)};
            extra.take(len % 2);
            if (key == "lsct" || key == "lsdk") rec.section = item.dword();
            if (key == "luni") {
                const auto units = item.dword(); if (units > 4096) return Errc::CorruptData;
                std::string text;
                for (u32 i=0; i<units; ++i) { u32 c = item.word(); if (c>=0xd800 && c<=0xdbff && i+1<units) { const auto low=item.word(); ++i; if (low<0xdc00 || low>0xdfff) return Errc::CorruptData; c=0x10000+((c-0xd800)<<10)+(low-0xdc00); } if (c>=0xd800 && c<=0xdfff) return Errc::CorruptData; utf8(text,c); }
                l.name = std::move(text);
            }
            if (key == "TySh" || key == "vmsk" || key == "lfx2" || key == "lrFX" || key == "SoLd") doc.approximated = true;
            if (!item.ok) return Errc::CorruptData;
        }
        if (!info.ok || !extra.ok) return Errc::CorruptData;
        const usize size = static_cast<usize>(l.width) * l.height * 4;
        if (size > kMaxDecodedBytes - budget) return Errc::BudgetExceeded;
        budget += size;
        if (rec.section == 0 && size) { l.rgba.assign(size, 0); for (usize p=3;p<size;p+=4) l.rgba[p]=255; }
    }
    for (auto& rec : records) {
        auto& l = rec.layer; std::vector<u8> raw, mask;
        for (const auto& c : rec.channels) {
            Reader data{info.take(c.length)}; if (!info.ok) return Errc::CorruptData;
            if (rec.section || (c.id < -2) || (c.id > (mode == 1 ? 0 : 2))) continue;
            const bool isMask = c.id == -2;
            const u32 w = isMask ? rec.mw : l.width, h = isMask ? rec.mh : l.height;
            if (!w || !h) continue;
            if (!channel(data,w,h,depth,raw)) return Errc::CorruptData;
            if (isMask) { mask = raw; continue; }
            for (usize p=0;p<static_cast<usize>(w)*h;++p) {
                const auto v = raw[p * (depth/8)];
                if (c.id == -1) l.rgba[p*4+3]=v;
                else if (mode==1) l.rgba[p*4]=l.rgba[p*4+1]=l.rgba[p*4+2]=v;
                else l.rgba[p*4+static_cast<usize>(c.id)]=v;
            }
        }
        if (!mask.empty() && !(rec.maskFlags & 2)) for (u32 y=0;y<l.height;++y) for (u32 x=0;x<l.width;++x) {
            const i64 mx=static_cast<i64>(l.left)+x-rec.mx, my=static_cast<i64>(l.top)+y-rec.my;
            u32 v=rec.maskDefault;
            if (mx>=0 && my>=0 && mx<rec.mw && my<rec.mh) v=mask[(static_cast<usize>(my)*rec.mw+static_cast<usize>(mx))*(depth/8)];
            if (rec.maskFlags & 4) v=255-v;
            auto& a=l.rgba[(static_cast<usize>(y)*l.width+x)*4+3]; a=static_cast<u8>((a*v+127)/255);
        }
    }
    std::vector<i32> parents;
    for (auto& rec : records) {
        if (rec.section == 3) { if (parents.empty()) return Errc::CorruptData; parents.pop_back(); continue; }
        if (rec.clipping) doc.approximated = true;
        auto& l=rec.layer; l.parent=parents.empty() ? -1 : parents.back(); l.group=rec.section==1 || rec.section==2;
        if (l.group || !l.rgba.empty()) {
            const auto index=static_cast<i32>(doc.layers.size()); doc.layers.push_back(std::move(l));
            if (doc.layers.back().group) { if (parents.size()>=16) return Errc::BudgetExceeded; parents.push_back(index); }
        }
    }
    if (!parents.empty() || doc.layers.empty()) return Errc::CorruptData;
    out=std::move(doc); return OkStatus;
}
}
