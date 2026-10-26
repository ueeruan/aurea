// =============================================================================
//  Aurea / text / FontManager.cpp
// =============================================================================
#include "aurea/text/FontManager.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace aurea::text {

namespace {

u32 be32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]); }
u16 be16(const u8* p) { return static_cast<u16>((p[0] << 8) | p[1]); }

bool read_at(std::FILE* f, u64 off, void* dst, usize n) {
    if (std::fseek(f, static_cast<long>(off), SEEK_SET) != 0) return false;
    return std::fread(dst, 1, n, f) == n;
}

/// Nome (UTF-16BE da plataforma 3 ou Mac Roman da 1) → UTF-8.
std::string decode_name(const u8* p, u16 len, u16 platform) {
    std::string out;
    if (platform == 3 || platform == 0) {
        for (u16 i = 0; i + 1 < len; i += 2) {
            u32 c = be16(p + i);
            if (c >= 0xD800 && c <= 0xDBFF && i + 3 < len) {
                const u32 lo = be16(p + i + 2);
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
            if (c < 0x80) out += static_cast<char>(c);
            else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
            else if (c < 0x10000) { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
            else { out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        }
    } else {
        for (u16 i = 0; i < len; ++i) out += static_cast<char>(p[i] < 0x80 ? p[i] : '?');
    }
    return out;
}

u32 hash_path(const std::string& s) {
    u32 h = 2166136261u;
    for (char c : s) { h ^= static_cast<u8>(c); h *= 16777619u; }
    return h ? h : 1u;
}

// --- Omega conteúdo (área 06): categoria e escritas da face -----------------------

/// A face tem glifo para `cp`? Lê o `cmap` (formato 4 ou 12). `t` = a tabela inteira.
bool cmap_has(const std::vector<u8>& t, u32 cp) {
    if (t.size() < 4) return false;
    const u16 n = be16(&t[2]);
    u32 best = 0;
    int bestScore = -1;
    for (u16 i = 0; i < n && 4u + 8u * (i + 1u) <= t.size(); ++i) {
        const u8* r = &t[4 + 8u * i];
        const u16 plat = be16(r), enc = be16(r + 2);
        const u32 off = be32(r + 4);
        if (off + 2 > t.size()) continue;
        const u16 fmt = be16(&t[off]);
        const int score = (fmt == 12 && (plat == 3 || plat == 0)) ? 3 : (fmt == 4 && plat == 3 && enc == 1) ? 2 : (fmt == 4 && plat == 0) ? 1 : -1;
        if (score > bestScore) { bestScore = score; best = off; }
    }
    if (bestScore < 0) return false;
    const u16 fmt = be16(&t[best]);
    if (fmt == 12) {
        if (best + 16 > t.size()) return false;
        const u32 groups = be32(&t[best + 12]);
        for (u32 g = 0; g < groups && best + 16 + 12u * (g + 1u) <= t.size(); ++g) {
            const u8* r = &t[best + 16 + 12u * g];
            if (cp >= be32(r) && cp <= be32(r + 4)) return true;
        }
        return false;
    }
    if (cp > 0xFFFF || best + 14 > t.size()) return false;
    const u32 segX2 = be16(&t[best + 6]);
    const u32 ends = best + 14, starts = ends + segX2 + 2, deltas = starts + segX2, ranges = deltas + segX2;
    if (ranges + segX2 > t.size()) return false;
    for (u32 s2 = 0; s2 < segX2; s2 += 2) {
        const u16 end = be16(&t[ends + s2]);
        if (end < cp) continue;
        const u16 start = be16(&t[starts + s2]);
        if (start > cp) return false;
        const u16 delta = be16(&t[deltas + s2]);
        const u16 ro = be16(&t[ranges + s2]);
        if (ro == 0) return static_cast<u16>(cp + delta) != 0;
        const u32 at = ranges + s2 + ro + 2u * (cp - start);
        if (at + 2 > t.size()) return false;
        const u16 glyph = be16(&t[at]);
        return glyph != 0 && static_cast<u16>(glyph + delta) != 0;
    }
    return false;
}

std::string lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// Categoria pela classe IBM do OS/2, pelo PANOSE, pelo `post` e, na falta, pelo nome.
u8 font_category(i32 familyClass, const u8* panose, bool fixedPitch, const std::string& family) {
    const u8 type = panose ? panose[0] : 0, serif = panose ? panose[1] : 0, proportion = panose ? panose[3] : 0;
    const std::string name = lower_ascii(family);
    auto contains = [&](const char* w) { return name.find(w) != std::string::npos; };
    if (fixedPitch || (type == 2 && proportion == 9) || contains("mono") || contains("code") || contains("courier")) return kFontMono;
    if (familyClass == 10 || type == 3 || contains("script") || contains("hand") || contains("brush") || contains("cursive")) return kFontHandwriting;
    if (familyClass == 9 || familyClass == 12 || type == 4 || type == 5 || contains("display") || contains("decor")) return kFontDisplay;
    if ((familyClass >= 1 && familyClass <= 5) || familyClass == 7) return kFontSerif;
    if (familyClass == 8) return kFontSans;
    if (type == 2 && serif >= 2 && serif <= 10) return kFontSerif;
    if (contains("serif") && !contains("sans")) return kFontSerif;
    return kFontSans;
}

/// Escritas pelas faixas Unicode do OS/2 (bits 0 latim, 7 grego, 9 cirílico,
/// 11 hebraico, 13 árabe, 15 devanágari, 24 tailandês, 28/56 hangul,
/// 49/50 kana, 59 ideogramas CJK).
u32 scripts_from_ranges(const u32 r[4]) {
    auto bit = [&](u32 b) { return (r[b / 32] >> (b % 32)) & 1u; };
    u32 s = 0;
    if (bit(0) || bit(1) || bit(2) || bit(3)) s |= kScriptLatin;
    if (bit(7)) s |= kScriptGreek;
    if (bit(9)) s |= kScriptCyrillic;
    if (bit(11)) s |= kScriptHebrew;
    if (bit(13)) s |= kScriptArabic;
    if (bit(15)) s |= kScriptDevanagari;
    if (bit(24)) s |= kScriptThai;
    if (bit(28) || bit(56)) s |= kScriptKorean;
    if (bit(49) || bit(50)) s |= kScriptJapanese;
    if (bit(59)) s |= kScriptChinese;
    return s;
}

/// As mesmas escritas pelo `cmap` (um caractere típico de cada uma).
u32 scripts_from_cmap(const std::vector<u8>& cmap) {
    struct Probe { u32 cp; u32 bit; };
    static const Probe kProbes[] = {
        {0x0041, kScriptLatin}, {0x03B1, kScriptGreek}, {0x0416, kScriptCyrillic}, {0x05D0, kScriptHebrew},
        {0x0639, kScriptArabic}, {0x0915, kScriptDevanagari}, {0x0E01, kScriptThai}, {0xAC00, kScriptKorean},
        {0x3042, kScriptJapanese}, {0x4E2D, kScriptChinese},
    };
    u32 s = 0;
    for (const Probe& p : kProbes) if (cmap_has(cmap, p.cp)) s |= p.bit;
    return s;
}

} // namespace

bool read_font_info(const std::string& path, FontEntry& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    u8 head[12];
    bool ok = read_at(f, 0, head, 12);
    u64 base = 0;
    if (ok && std::memcmp(head, "ttcf", 4) == 0) {   // coleção: a 1ª face
        u8 off[4];
        ok = read_at(f, 12, off, 4);
        base = ok ? be32(off) : 0;
        ok = ok && read_at(f, base, head, 12);
    }
    const u32 tag = ok ? be32(head) : 0;
    if (!ok || (tag != 0x00010000u && tag != 0x4F54544Fu /*OTTO*/ && tag != 0x74727565u /*true*/)) { std::fclose(f); return false; }
    const u16 numTables = be16(head + 4);
    std::vector<u8> dir(static_cast<usize>(numTables) * 16);
    if (!read_at(f, base + 12, dir.data(), dir.size())) { std::fclose(f); return false; }
    u32 nameOff = 0, nameLen = 0, os2Off = 0, os2Len = 0, headOff = 0, postOff = 0, postLen = 0, cmapOff = 0, cmapLen = 0;
    for (u16 i = 0; i < numTables; ++i) {
        const u8* r = &dir[i * 16u];
        const u32 t = be32(r), o = be32(r + 8), l = be32(r + 12);
        if (t == 0x6E616D65u) { nameOff = o; nameLen = l; }        // name
        else if (t == 0x4F532F32u) { os2Off = o; os2Len = l; }     // OS/2
        else if (t == 0x68656164u) { headOff = o; }                // head
        else if (t == 0x706F7374u) { postOff = o; postLen = l; }   // post (Omega conteúdo)
        else if (t == 0x636D6170u) { cmapOff = o; cmapLen = l; }   // cmap (Omega conteúdo)
    }
    std::string fam1, fam16, sub2, sub17;
    if (nameOff && nameLen >= 6 && nameLen < (1u << 20)) {
        std::vector<u8> nm(nameLen);
        if (read_at(f, nameOff, nm.data(), nm.size())) {
            const u16 count = be16(&nm[2]), strOff = be16(&nm[4]);
            // Preferência: Windows inglês (3/1/0x409), depois qualquer Windows, depois Mac.
            int bestScore[4] = {-1, -1, -1, -1};
            for (u16 i = 0; i < count && 6u + 12u * (i + 1u) <= nameLen; ++i) {
                const u8* r = &nm[6 + 12u * i];
                const u16 plat = be16(r), enc = be16(r + 2), lang = be16(r + 4), id = be16(r + 6), len = be16(r + 8), off = be16(r + 10);
                int slot = id == 1 ? 0 : id == 2 ? 1 : id == 16 ? 2 : id == 17 ? 3 : -1;
                if (slot < 0 || static_cast<u32>(strOff) + off + len > nameLen) continue;
                const int score = (plat == 3 && lang == 0x409) ? 3 : plat == 3 ? 2 : plat == 0 ? 1 : 0;
                if (score <= bestScore[slot]) continue;
                (void)enc;
                bestScore[slot] = score;
                const std::string v = decode_name(&nm[strOff + off], len, plat);
                (slot == 0 ? fam1 : slot == 1 ? sub2 : slot == 2 ? fam16 : sub17) = v;
            }
        }
    }
    out.family = !fam16.empty() ? fam16 : fam1;
    out.style = !sub17.empty() ? sub17 : (!sub2.empty() ? sub2 : "Regular");
    out.weight = 400;
    out.italic = false;
    // Omega conteúdo (área 06): classe IBM, PANOSE e faixas Unicode (OS/2),
    // passo fixo (post) e, sem faixas, o cmap.
    i32 familyClass = 0;
    u8 panose[10]{};
    u32 ranges[4]{};
    bool haveRanges = false;
    if (os2Off && os2Len >= 64) {
        u8 os2[64];
        if (read_at(f, os2Off, os2, 64)) {
            const u16 w = be16(os2 + 4);
            if (w >= 1 && w <= 1000) out.weight = w < 100 ? static_cast<u16>(w * 100) : w;
            out.italic = (be16(os2 + 62) & 1u) != 0;
            familyClass = static_cast<i32>(os2[30]);   // byte alto de sFamilyClass
            std::memcpy(panose, os2 + 32, 10);
            for (int k = 0; k < 4; ++k) ranges[k] = be32(os2 + 42 + 4 * k);
            haveRanges = (ranges[0] | ranges[1] | ranges[2] | ranges[3]) != 0;
        }
    } else if (headOff) {
        u8 hd[46];
        if (read_at(f, headOff, hd, 46)) {
            const u16 mac = be16(hd + 44);
            out.weight = (mac & 1u) ? 700 : 400;
            out.italic = (mac & 2u) != 0;
        }
    }
    bool fixedPitch = false;
    if (postOff && postLen >= 16) {
        u8 post[16];
        if (read_at(f, postOff, post, 16)) fixedPitch = be32(post + 12) != 0;
    }
    if (haveRanges) {
        out.scripts = scripts_from_ranges(ranges);
    } else if (cmapOff && cmapLen >= 4 && cmapLen <= (16u << 20)) {
        std::vector<u8> cmap(cmapLen);
        if (read_at(f, cmapOff, cmap.data(), cmap.size())) out.scripts = scripts_from_cmap(cmap);
    }
    std::fclose(f);
    if (out.family.empty()) out.family = std::filesystem::path(path).stem().string();
    out.category = font_category(familyClass, panose, fixedPitch, out.family);
    if (out.scripts == 0) out.scripts = kScriptLatin;
    out.path = path;
    out.id = hash_path(path);
    return true;
}

FontManager& FontManager::instance() {
    static FontManager m;
    return m;
}

void FontManager::set_system_dirs(std::vector<std::string> dirs) {
    std::lock_guard<std::mutex> lock(mutex_);
    dirs_ = std::move(dirs);
    scanned_ = false;
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const FontEntry& e) { return !e.imported; }), entries_.end());
}

void FontManager::set_imported_dirs(std::vector<std::string> dirs) {
    std::lock_guard<std::mutex> lock(mutex_);
    importedDirs_ = std::move(dirs);
    scanned_ = false;
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const FontEntry& e) { return !e.imported; }), entries_.end());
}

void FontManager::set_path_resolver(std::function<std::string(const std::string&)> fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    resolver_ = std::move(fn);
}

void FontManager::scan_locked() {
    if (scanned_) return;
    scanned_ = true;
    std::vector<std::string> dirs = dirs_;
    if (dirs.empty()) dirs = {"/system/fonts", "/product/fonts", "C:/Windows/Fonts", "/System/Library/Fonts", "/Library/Fonts"};
    std::error_code ec;
    usize before = entries_.size();
    for (const std::string& d : dirs) {
        for (std::filesystem::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
            FontEntry e;
            if (read_font_info(it->path().string(), e)) entries_.push_back(std::move(e));
        }
        ec.clear();
    }
    AUREA_LOG_INFO("fontes do sistema: %zu", entries_.size() - before);
    // Importadas em sessões anteriores: o arquivo ficou na pasta do app, mas o
    // registro era só em memória — sumiam da lista ao reabrir.
    before = entries_.size();
    for (const std::string& d : importedDirs_) {
        for (std::filesystem::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string name = it->path().filename().string();
            if (name.rfind("importando.", 0) == 0) continue;   // cópia pela metade
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
            const std::string path = it->path().string();
            if (std::any_of(entries_.begin(), entries_.end(), [&](const FontEntry& x) { return x.path == path; })) continue;
            FontEntry e;
            if (!read_font_info(path, e)) continue;
            e.imported = true;
            entries_.push_back(std::move(e));
        }
        ec.clear();
    }
    AUREA_LOG_INFO("fontes importadas: %zu", entries_.size() - before);
}

std::vector<FontEntry> FontManager::list() {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_locked();
    std::vector<FontEntry> v = entries_;
    std::sort(v.begin(), v.end(), [](const FontEntry& a, const FontEntry& b) {
        if (a.family != b.family) return a.family < b.family;
        if (a.italic != b.italic) return !a.italic;
        return a.weight < b.weight;
    });
    return v;
}

const FontEntry* FontManager::add_file(const std::string& path, bool imported) {
    FontEntry e;
    if (!read_font_info(path, e)) return nullptr;
    if (!Font::load(path)) return nullptr;   // legível de verdade (stb aceita)
    e.imported = imported;
    std::lock_guard<std::mutex> lock(mutex_);
    for (FontEntry& x : entries_) {
        if (x.path == path) { x = e; return &x; }
    }
    entries_.push_back(std::move(e));
    return &entries_.back();
}

bool FontManager::remove_imported(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const FontEntry& e) { return e.path == path; });
    if (it == entries_.end() || !it->imported) return false;
    entries_.erase(it);
    cache_.erase(path);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return true;
}

std::string FontManager::resolve(const std::string& stored) {
    std::lock_guard<std::mutex> lock(mutex_);
    return resolver_ ? resolver_(stored) : stored;
}

std::shared_ptr<const Font> FontManager::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = cache_.find(path); it != cache_.end()) return it->second;
    auto f = Font::load(path);
    if (f) cache_[path] = f;
    return f;
}

std::shared_ptr<const Font> FontManager::font_for(const TextData& t) {
    if (!t.fontPath.empty()) {
        std::string real;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            real = resolver_ ? resolver_(t.fontPath) : t.fontPath;
        }
        if (auto f = load(real)) return f;
    }
    if (!t.fontFamily.empty()) {
        std::string best;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            scan_locked();
            int bestScore = 1 << 30;
            for (const FontEntry& e : entries_) {
                if (e.family != t.fontFamily) continue;
                const int score = std::abs(static_cast<int>(e.weight) - static_cast<int>(t.fontWeight)) + (e.italic != t.fontItalic ? 1000 : 0);
                if (score < bestScore) { bestScore = score; best = e.path; }
            }
        }
        if (!best.empty()) {
            if (auto f = load(best)) return f;
        }
    }
    return default_font();
}

} // namespace aurea::text
