// =============================================================================
//  Aurea / tests / test_hdri.cpp
//
//  Leitor de panoramas do ambiente 3D (scene3d::decode_hdri_detailed) e o
//  import pelo motor (Engine::import_hdri): Radiance em todas as variantes,
//  OpenEXR (tinyexr), .zip, redução de panorama enorme e os códigos de erro
//  que a UI traduz. Arquivos REAIS do Poly Haven em build/reference/hdri
//  (fora do git) entram quando existem.
// =============================================================================
#include "TestFramework.hpp"
#include "aurea/Engine.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/scene3d/Environment.hpp"

#include "tinyexr.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::scene3d;

namespace {

/// Panorama de referência: gradiente com valores HDR (> 1).
f32 ref(u32 x, u32 y, u32 c, u32 w, u32 h) {
    return (0.05f + 3.0f * static_cast<f32>(x) / static_cast<f32>(w)) * (c == 0 ? 1.0f : c == 1 ? 0.5f : 0.25f) +
           2.0f * static_cast<f32>(y) / static_cast<f32>(h);
}

void rgbe(f32 r, f32 g, f32 b, u8* out) {
    const f32 m = std::max(r, std::max(g, b));
    if (m < 1e-32f) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    int e = 0;
    const f32 f = std::frexp(m, &e) * 256.0f / m;
    out[0] = static_cast<u8>(r * f); out[1] = static_cast<u8>(g * f); out[2] = static_cast<u8>(b * f);
    out[3] = static_cast<u8>(e + 128);
}

enum class Scan { NewRle, Flat, OldRle };

/// Radiance escrito à mão. `rows` = linhas do arquivo de cima (-Y) ou de baixo (+Y).
std::vector<u8> make_hdr(u32 w, u32 h, Scan mode, const char* magic = "#?RADIANCE", bool crlf = false,
                         bool yUp = false, bool xLeft = false) {
    std::string head = std::string(magic) + (crlf ? "\r\n" : "\n");
    head += std::string("# gerado pelo teste") + (crlf ? "\r\n" : "\n");
    head += std::string("FORMAT=32-bit_rle_rgbe") + (crlf ? "\r\n\r\n" : "\n\n");
    char res[64];
    std::snprintf(res, sizeof(res), "%cY %u %cX %u\n", yUp ? '+' : '-', h, xLeft ? '-' : '+', w);
    head += res;
    std::vector<u8> out(head.begin(), head.end());
    std::vector<u8> px(static_cast<usize>(w) * 4);
    for (u32 s = 0; s < h; ++s) {
        const u32 y = yUp ? h - 1 - s : s;
        for (u32 i = 0; i < w; ++i) {
            const u32 x = xLeft ? w - 1 - i : i;
            rgbe(ref(x, y, 0, w, h), ref(x, y, 1, w, h), ref(x, y, 2, w, h), &px[static_cast<usize>(i) * 4]);
        }
        if (mode == Scan::Flat) { out.insert(out.end(), px.begin(), px.end()); continue; }
        if (mode == Scan::OldRle) {
            // Cada pixel seguido de (1,1,1,0)? Não: um pixel e depois uma
            // repetição dele mesmo (pixels iguais aos pares no gradiente lento).
            for (u32 i = 0; i < w;) {
                out.insert(out.end(), &px[i * 4], &px[i * 4] + 4);
                u32 run = 0;
                while (i + 1 + run < w && run < 255 && std::memcmp(&px[(i + 1 + run) * 4], &px[i * 4], 4) == 0) ++run;
                if (run) { const u8 rep[4] = {1, 1, 1, static_cast<u8>(run)}; out.insert(out.end(), rep, rep + 4); }
                i += 1 + run;
            }
            continue;
        }
        const u8 hdr[4] = {2, 2, static_cast<u8>(w >> 8), static_cast<u8>(w & 0xff)};
        out.insert(out.end(), hdr, hdr + 4);
        for (u32 c = 0; c < 4; ++c) {
            // Repetições quando há ≥ 3 iguais; o resto vai literal (o mesmo esquema do Radiance).
            u32 i = 0;
            while (i < w) {
                u32 run = 1;
                while (i + run < w && run < 127 && px[(i + run) * 4 + c] == px[i * 4 + c]) ++run;
                if (run >= 3) { out.push_back(static_cast<u8>(128 + run)); out.push_back(px[i * 4 + c]); i += run; continue; }
                u32 lit = 0;
                while (i + lit < w && lit < 128) {
                    u32 ahead = 1;
                    while (i + lit + ahead < w && ahead < 3 && px[(i + lit + ahead) * 4 + c] == px[(i + lit) * 4 + c]) ++ahead;
                    if (ahead >= 3) break;
                    ++lit;
                }
                out.push_back(static_cast<u8>(lit));
                for (u32 k = 0; k < lit; ++k) out.push_back(px[(i + k) * 4 + c]);
                i += lit;
            }
        }
    }
    return out;
}

f32 max_error(const HdriPixels& p) {
    f32 worst = 0;
    for (u32 y = 0; y < p.height; ++y)
        for (u32 x = 0; x < p.width; ++x)
            for (u32 c = 0; c < 3; ++c) {
                // RGBE divide um expoente pelos três canais: o erro é relativo ao maior.
                const f32 want = ref(x, y, c, p.width, p.height);
                const f32 top = std::max(ref(x, y, 0, p.width, p.height), std::max(ref(x, y, 1, p.width, p.height), ref(x, y, 2, p.width, p.height)));
                worst = std::max(worst, std::fabs(p.rgb[(static_cast<usize>(y) * p.width + x) * 3 + c] - want) / top);
            }
    return worst;
}

std::vector<u8> make_exr(u32 w, u32 h, bool half) {
    std::vector<f32> rgb(static_cast<usize>(w) * h * 3);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x)
            for (u32 c = 0; c < 3; ++c) rgb[(static_cast<usize>(y) * w + x) * 3 + c] = ref(x, y, c, w, h);
    unsigned char* mem = nullptr;
    const char* err = nullptr;
    // ZIP por padrão: exercita o inflate do stb no caminho de leitura.
    const int n = SaveEXRToMemory(rgb.data(), static_cast<int>(w), static_cast<int>(h), 3, half ? 1 : 0, &mem, &err);
    if (n <= 0 || !mem) { if (err) FreeEXRErrorMessage(err); return {}; }
    std::vector<u8> out(mem, mem + n);
    std::free(mem);
    return out;
}

void put16(std::vector<u8>& v, u32 x) { v.push_back(static_cast<u8>(x)); v.push_back(static_cast<u8>(x >> 8)); }
void put32(std::vector<u8>& v, u32 x) { put16(v, x & 0xffff); put16(v, x >> 16); }

/// Zip de um arquivo: `deflate` = método 8 com blocos "stored" (um deflate válido).
std::vector<u8> make_zip(const std::string& name, const std::vector<u8>& data, bool deflate) {
    std::vector<u8> body = data;
    if (deflate) {
        body.clear();
        for (usize i = 0; i < data.size() || i == 0;) {
            const usize len = std::min<usize>(65535, data.size() - i);
            body.push_back(i + len >= data.size() ? 1 : 0);
            put16(body, static_cast<u32>(len)); put16(body, static_cast<u32>(~len & 0xffff));
            body.insert(body.end(), data.begin() + static_cast<std::ptrdiff_t>(i), data.begin() + static_cast<std::ptrdiff_t>(i + len));
            i += len;
            if (len == 0) break;
        }
    }
    std::vector<u8> z;
    // Um arquivo-isca antes (pasta do macOS): precisa ser ignorado.
    const std::string junk = "__MACOSX/._" + name;
    auto local = [&](const std::string& n, const std::vector<u8>& b, u32 method, u32 raw) {
        const u32 at = static_cast<u32>(z.size());
        put32(z, 0x04034b50); put16(z, 20); put16(z, 0); put16(z, method); put16(z, 0); put16(z, 0);
        put32(z, 0); put32(z, static_cast<u32>(b.size())); put32(z, raw);
        put16(z, static_cast<u32>(n.size())); put16(z, 0);
        z.insert(z.end(), n.begin(), n.end()); z.insert(z.end(), b.begin(), b.end());
        return at;
    };
    const std::vector<u8> junkData = {'x', 'y'};
    const u32 junkAt = local(junk, junkData, 0, 2);
    const u32 fileAt = local(name, body, deflate ? 8 : 0, static_cast<u32>(data.size()));
    const u32 dir = static_cast<u32>(z.size());
    auto central = [&](const std::string& n, u32 size, u32 raw, u32 method, u32 at) {
        put32(z, 0x02014b50); put16(z, 20); put16(z, 20); put16(z, 0); put16(z, method); put16(z, 0); put16(z, 0);
        put32(z, 0); put32(z, size); put32(z, raw); put16(z, static_cast<u32>(n.size())); put16(z, 0); put16(z, 0);
        put16(z, 0); put16(z, 0); put32(z, 0); put32(z, at);
        z.insert(z.end(), n.begin(), n.end());
    };
    central(junk, 2, 2, 0, junkAt);
    central(name, static_cast<u32>(body.size()), static_cast<u32>(data.size()), deflate ? 8 : 0, fileAt);
    const u32 dirSize = static_cast<u32>(z.size()) - dir;
    put32(z, 0x06054b50); put16(z, 0); put16(z, 0); put16(z, 2); put16(z, 2); put32(z, dirSize); put32(z, dir); put16(z, 0);
    return z;
}

struct TempDir {
    std::filesystem::path root;
    TempDir() {
        root = std::filesystem::temp_directory_path() /
               ("aurea_hdri_formats_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code e;
        std::filesystem::create_directories(root, e);
    }
    ~TempDir() {
        std::error_code e;
        if (root.filename().string().rfind("aurea_hdri_formats_", 0) == 0) std::filesystem::remove_all(root, e);
    }
    std::string write(const std::string& rel, const std::vector<u8>& bytes) const {
        const auto p = root / std::filesystem::u8path(rel);
        std::error_code e;
        std::filesystem::create_directories(p.parent_path(), e);
        const auto u = p.generic_u8string();
        const std::string s(reinterpret_cast<const char*>(u.data()), u.size());
        FILE* f = fileio::open_file(s, "wb");
        if (f) { std::fwrite(bytes.data(), 1, bytes.size(), f); std::fclose(f); }
        return s;
    }
};

EngineConfig engine_config(const std::string& docs = {}) {
    EngineConfig cfg;
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    cfg.documentsDirectory = docs;
    return cfg;
}

} // namespace

AUREA_TEST(Hdri, RadianceAllVariantsDecodeToTheSamePanorama) {
    struct Case { const char* name; std::vector<u8> bytes; };
    const u32 w = 96, h = 48;
    std::vector<Case> cases;
    cases.push_back({"RLE novo (Poly Haven)", make_hdr(w, h, Scan::NewRle)});
    cases.push_back({"#?RGBE", make_hdr(w, h, Scan::NewRle, "#?RGBE")});
    cases.push_back({"CRLF", make_hdr(w, h, Scan::NewRle, "#?RADIANCE", true)});
    cases.push_back({"plano", make_hdr(w, h, Scan::Flat)});
    cases.push_back({"RLE antigo", make_hdr(w, h, Scan::OldRle)});
    cases.push_back({"+Y (de baixo para cima)", make_hdr(w, h, Scan::NewRle, "#?RADIANCE", false, true)});
    cases.push_back({"-X (espelhado)", make_hdr(w, h, Scan::NewRle, "#?RADIANCE", false, false, true)});
    cases.push_back({"linha curta (< 8 px)", make_hdr(6, 3, Scan::Flat)});
    for (const Case& c : cases) {
        const HdriDecode d = decode_hdri_detailed(c.bytes.data(), c.bytes.size());
        AUREA_CHECK_MSG(d.status == HdriStatus::Ok && d.pixels, c.name);
        if (!d.pixels) continue;
        AUREA_CHECK(!d.pixels->ldr);
        const f32 err = max_error(*d.pixels);
        std::printf("\n    %-26s %ux%u erro relativo %.4f", c.name, d.pixels->width, d.pixels->height, static_cast<double>(err));
        AUREA_CHECK_MSG(err < 0.01f, c.name);   // precisão do RGBE (8 bits de mantissa)
    }
    // A API antiga continua igual (é o que hdri_lookup e os testes de GPU usam).
    const auto rle = make_hdr(w, h, Scan::NewRle);
    const auto legacy = decode_hdri(rle.data(), rle.size());
    AUREA_CHECK(legacy && legacy->width == w && legacy->height == h);
}

AUREA_TEST(Hdri, BrokenOrForeignFilesSayWhy) {
    auto full = make_hdr(64, 32, Scan::NewRle);
    std::vector<u8> cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(full.size() * 2 / 3));
    AUREA_CHECK(decode_hdri_detailed(cut.data(), cut.size()).status == HdriStatus::Corrupt);
    const std::string html = "<!DOCTYPE html><html><body>404</body></html>";
    AUREA_CHECK(decode_hdri_detailed(reinterpret_cast<const u8*>(html.data()), html.size()).status == HdriStatus::UnsupportedFormat);
    const u8 tiff[16] = {'I', 'I', 42, 0, 8, 0, 0, 0};
    AUREA_CHECK(decode_hdri_detailed(tiff, sizeof(tiff)).status == HdriStatus::UnsupportedFormat);
    const std::string badRes = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\nY 4 X 4\n";
    AUREA_CHECK(decode_hdri_detailed(reinterpret_cast<const u8*>(badRes.data()), badRes.size()).status == HdriStatus::Corrupt);
    const std::string huge = "#?RADIANCE\n\n-Y 100000 +X 200000\n";
    AUREA_CHECK(decode_hdri_detailed(reinterpret_cast<const u8*>(huge.data()), huge.size()).status == HdriStatus::TooLarge);
    auto exr = make_exr(32, 16, false);
    AUREA_CHECK(!exr.empty());
    exr.resize(exr.size() / 2);
    AUREA_CHECK(decode_hdri_detailed(exr.data(), exr.size()).status == HdriStatus::Corrupt);
}

AUREA_TEST(Hdri, HugePanoramaIsReducedInsteadOfRefused) {
    // 9000×1000 = 9 MP > teto (8,4 MP): sai 4500×500, cada texel = média 2×2.
    const u32 w = 9000, h = 1000;
    const auto bytes = make_hdr(w, h, Scan::NewRle);
    const HdriDecode d = decode_hdri_detailed(bytes.data(), bytes.size());
    AUREA_CHECK(d.status == HdriStatus::Ok && d.pixels);
    if (!d.pixels) return;
    AUREA_CHECK_EQ(d.pixels->width, 4500u);
    AUREA_CHECK_EQ(d.pixels->height, 500u);
    AUREA_CHECK(static_cast<u64>(d.pixels->width) * d.pixels->height <= kHdriMaxPixels);
    for (auto [ox, oy] : {std::pair<u32, u32>{0, 0}, {2250, 250}, {4499, 499}}) {
        f32 want = 0;
        for (u32 dy = 0; dy < 2; ++dy)
            for (u32 dx = 0; dx < 2; ++dx) want += ref(ox * 2 + dx, oy * 2 + dy, 0, w, h) * 0.25f;
        AUREA_CHECK_NEAR(d.pixels->rgb[(static_cast<usize>(oy) * 4500 + ox) * 3], want, want * 0.02f);
    }
}

AUREA_TEST(Hdri, OpenExrFloatAndHalfLoadIntoAValidEnvironment) {
    for (bool half : {false, true}) {
        const u32 w = 128, h = 64;
        const auto bytes = make_exr(w, h, half);
        AUREA_CHECK_MSG(!bytes.empty(), "tinyexr nao gravou o EXR");
        if (bytes.empty()) continue;
        const HdriDecode d = decode_hdri_detailed(bytes.data(), bytes.size());
        AUREA_CHECK_MSG(d.status == HdriStatus::Ok && d.pixels, half ? "EXR half" : "EXR float");
        if (!d.pixels) continue;
        AUREA_CHECK_EQ(d.pixels->width, w);
        AUREA_CHECK_EQ(d.pixels->height, h);
        const f32 err = max_error(*d.pixels);
        AUREA_CHECK(err < (half ? 2e-3f : 1e-6f));
        // O ambiente sai do mesmo jeito que de um .hdr: irradiância e especular cheios e finitos.
        const EnvironmentMaps maps = build_environment_from_equirect(d.pixels->rgb.data(), w, h, 64u);
        AUREA_CHECK(maps.irradiance.size > 0 && !maps.irradiance.levels.empty());
        AUREA_CHECK(maps.prefiltered.size > 0);
        bool finite = true, lit = false;
        for (u16 v : maps.irradiance.levels[0]) {
            const f32 f = half_to_float(v);
            finite = finite && std::isfinite(f);
            lit = lit || f > 0.1f;
        }
        AUREA_CHECK(finite && lit);
    }
}

AUREA_TEST(Hdri, ZipWithThePanoramaInsideIsAccepted) {
    const auto hdr = make_hdr(64, 32, Scan::NewRle);
    for (bool deflate : {false, true}) {
        const auto zip = make_zip("sky_puresky_1k.hdr", hdr, deflate);
        const HdriDecode d = decode_hdri_detailed(zip.data(), zip.size());
        AUREA_CHECK_MSG(d.status == HdriStatus::Ok && d.pixels, deflate ? "zip deflate" : "zip stored");
        if (d.pixels) AUREA_CHECK(max_error(*d.pixels) < 0.01f);
    }
    const auto exr = make_exr(32, 16, true);
    const auto zipExr = make_zip("Sky.EXR", exr, true);
    AUREA_CHECK(decode_hdri_detailed(zipExr.data(), zipExr.size()).status == HdriStatus::Ok);
    const std::vector<u8> text = {'o', 'l', 'a'};
    const auto noPanorama = make_zip("leia-me.txt", text, false);
    AUREA_CHECK(decode_hdri_detailed(noPanorama.data(), noPanorama.size()).status == HdriStatus::UnsupportedFormat);
}

AUREA_TEST(Hdri, ImportReportsSpecificErrorCodes) {
    TempDir dir;
    Engine e;
    AUREA_CHECK(e.initialize(engine_config()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, nullptr).ok());
    const auto missing = (dir.root / "nao-existe.hdr").generic_string();
    const auto r1 = e.import_hdri(missing.c_str());
    AUREA_CHECK(!r1.ok() && r1.status().code() == Errc::IoError);
    auto hdr = make_hdr(64, 32, Scan::NewRle);
    hdr.resize(hdr.size() / 2);
    const auto r2 = e.import_hdri(dir.write("cortado.hdr", hdr).c_str());
    AUREA_CHECK(!r2.ok() && r2.status().code() == Errc::CorruptData);
    const std::string html = "<html>erro</html>";
    const auto r3 = e.import_hdri(dir.write("pagina.hdr", std::vector<u8>(html.begin(), html.end())).c_str());
    AUREA_CHECK(!r3.ok() && r3.status().code() == Errc::UnsupportedFormat);
    const auto r4 = e.import_hdri(dir.write("ceu.exr", make_exr(64, 32, false)).c_str());
    AUREA_CHECK(r4.ok());
    const auto r5 = e.import_hdri(dir.write("ceu.zip", make_zip("ceu.hdr", make_hdr(64, 32, Scan::NewRle), true)).c_str());
    AUREA_CHECK(r5.ok());
    f32 env[3]{};
    AUREA_CHECK(e.query_environment(env) && env[0] == 1.0f);
    e.shutdown();
}

// Erro 17 da beta: o app de produção (com.aurea.aurea) copia o .hdr para
// files/projetos/modelos e passa o caminho ABSOLUTO; o resolvedor de caminhos
// antigos o reescrevia para files/projetos/projetos/… (inexistente).
AUREA_TEST(Hdri, AndroidReleaseSandboxPathImports) {
    TempDir dir;
    const auto docs = dir.root / "files" / "projetos";
    std::error_code ec;
    std::filesystem::create_directories(docs, ec);
    dir.write("files/projetos/modelos/abc123.hdr", make_hdr(64, 32, Scan::NewRle));
    Engine e;
    const auto du = docs.generic_u8string();
    AUREA_CHECK(e.initialize(engine_config(std::string(reinterpret_cast<const char*>(du.data()), du.size()))).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, nullptr).ok());
    for (const char* path : {"/data/user/0/com.aurea.aurea/files/projetos/modelos/abc123.hdr",
                             "/data/data/com.aurea.aurea/files/projetos/modelos/abc123.hdr"}) {
        const auto r = e.import_hdri(path);
        AUREA_CHECK_MSG(r.ok(), path);
        if (r.ok()) AUREA_CHECK_EQ(e.project()->asset(AssetId::unpack(*r))->sourcePath, std::string("docs:modelos/abc123.hdr"));
    }
    // Nada fora da pasta do app e nada inexistente.
    AUREA_CHECK(!e.import_hdri("/data/user/0/com.aurea.aurea/files/projetos/modelos/outro.hdr").ok());
    AUREA_CHECK(!e.import_hdri("/data/user/0/com.aurea.aurea/files/projetos/../../segredo.hdr").ok());
    e.shutdown();
}

// Os arquivos reais do relato (Poly Haven, 1k/2k, .hdr e .exr PIZ float) —
// baixados à mão para build/reference/hdri; sem eles o teste só avisa.
AUREA_TEST(Hdri, RealPolyHavenFilesWhenPresent) {
    const auto dir = std::filesystem::u8path(AUREA_TEST_DATA_DIR) / ".." / ".." / ".." / "build" / "reference" / "hdri";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) { std::printf("\n    build/reference/hdri ausente: pulado"); return; }
    u32 seen = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const auto ext = entry.path().extension().string();
        if (ext != ".hdr" && ext != ".exr") continue;
        const auto u = entry.path().generic_u8string();
        const std::string path(reinterpret_cast<const char*>(u.data()), u.size());
        FILE* f = fileio::open_file(path, "rb");
        if (!f) continue;
        std::fseek(f, 0, SEEK_END);
        std::vector<u8> bytes(static_cast<usize>(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        const bool ok = std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
        std::fclose(f);
        if (!ok) continue;
        const auto t0 = std::chrono::steady_clock::now();
        const HdriDecode d = decode_hdri_detailed(bytes.data(), bytes.size());
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("\n    %s: status %u %ux%u %.0f ms", entry.path().filename().string().c_str(), static_cast<unsigned>(d.status),
                    d.pixels ? d.pixels->width : 0, d.pixels ? d.pixels->height : 0, ms);
        AUREA_CHECK_MSG(d.status == HdriStatus::Ok && d.pixels, path.c_str());
        if (!d.pixels) continue;
        f64 sum = 0;
        for (f32 v : d.pixels->rgb) sum += v;
        AUREA_CHECK(sum > 0.0 && std::isfinite(sum));
        ++seen;
    }
    std::printf("\n    %u arquivo(s) reais lidos", seen);
}
