// =============================================================================
//  Importação do After Effects (project/AeImport.hpp): .ffx sintéticos
//  escritos aqui (sempre rodam), presets reais do AE quando a pasta de
//  referência existe (pulam sem ela) e scripts .jsx no sandbox.
//
//  Presets reais: build/reference/ae2026/fixtures (cópia local, fora do
//  produto) ou a variável AUREA_AE_FIXTURES; AUREA_AE_PRESETS aponta para a
//  pasta Presets do AE instalado e liga a varredura de cobertura.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/project/AeImport.hpp"
#include "aurea/text/TextAnimator.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace aurea;

namespace {

// --- escritor de RIFX big-endian (o mesmo formato que o leitor documenta) ----
std::string be32(u32 v) {
    std::string s(4, '\0');
    s[0] = static_cast<char>(v >> 24); s[1] = static_cast<char>(v >> 16);
    s[2] = static_cast<char>(v >> 8); s[3] = static_cast<char>(v);
    return s;
}
std::string be16(u32 v) { std::string s(2, '\0'); s[0] = static_cast<char>(v >> 8); s[1] = static_cast<char>(v); return s; }
std::string bed(f64 d) {
    u64 v;
    std::memcpy(&v, &d, 8);
    std::string s(8, '\0');
    for (int i = 7; i >= 0; --i) { s[static_cast<usize>(i)] = static_cast<char>(v & 0xFF); v >>= 8; }
    return s;
}
std::string bef(f32 f) { u32 v; std::memcpy(&v, &f, 4); return be32(v); }
std::string chunk(const char* id, const std::string& body) {
    std::string s(id, 4);
    s += be32(static_cast<u32>(body.size()));
    s += body;
    if (body.size() & 1u) s.push_back('\0');
    return s;
}
std::string list(const char* type, const std::string& body) { return chunk("LIST", std::string(type, 4) + body); }
std::string tdmn(const std::string& m) { std::string b = m; b.resize(40, '\0'); return chunk("tdmn", b); }
std::string tdsn(const std::string& n) { return chunk("tdsn", "Utf8" + be32(static_cast<u32>(n.size())) + n); }
std::string tdsb(u32 f) { return chunk("tdsb", be32(f)); }
std::string tdb4(u32 dims, bool spatial, u32 timescale) {
    std::string b = "\xdb\x99" + be16(dims) + (spatial ? be32(0x000e0003u) : be32(0)) + be32(0xFFFFFFFFu) + be32(timescale);
    b.resize(124, '\0');
    return chunk("tdb4", b);
}
std::string cdat(const std::vector<f64>& v) {
    std::string b;
    for (f64 x : v) b += bed(x);
    b.resize(std::max<usize>(b.size(), 40), '\0');
    return chunk("cdat", b);
}

struct FKey {
    u32 ticks;
    std::vector<f64> v;
    u8 in = 1, out = 1;
    std::vector<std::pair<f64, f64>> inEase, outEase;   // (velocidade, influência 0..1)
};

std::string keylist(const std::vector<FKey>& keys, u32 dims, bool spatial) {
    const u32 size = spatial ? 56 + 24 * dims : 8 + 40 * dims;
    std::string hd = be32(0x00d00bee) + be32(0) + be32(static_cast<u32>(keys.size())) + be32(1) + be32(size) + be32(4);
    hd.resize(52, '\0');
    std::string data;
    for (const FKey& k : keys) {
        std::string e = be32(k.ticks) + std::string(1, static_cast<char>(k.in)) + std::string(1, static_cast<char>(k.out)) + be16(0);
        auto ease = [&](const std::vector<std::pair<f64, f64>>& v, u32 c) { return c < v.size() ? v[c] : std::pair<f64, f64>{0.0, 1.0 / 6.0}; };
        if (spatial) {
            e += be32(1) + be32(0) + bed(0);
            e += bed(ease(k.inEase, 0).first) + bed(ease(k.inEase, 0).second) + bed(ease(k.outEase, 0).first) + bed(ease(k.outEase, 0).second);
            for (u32 c = 0; c < dims; ++c) e += bed(k.v[c]);
            for (u32 c = 0; c < 2 * dims; ++c) e += bed(0);
        } else {
            for (u32 c = 0; c < dims; ++c) e += bed(k.v[c]);
            for (u32 c = 0; c < dims; ++c) e += bed(ease(k.inEase, c).first);
            for (u32 c = 0; c < dims; ++c) e += bed(ease(k.inEase, c).second);
            for (u32 c = 0; c < dims; ++c) e += bed(ease(k.outEase, c).first);
            for (u32 c = 0; c < dims; ++c) e += bed(ease(k.outEase, c).second);
        }
        data += e;
    }
    return list("list", chunk("lhd3", hd) + chunk("ldat", data));
}

std::string prop(const std::string& name, u32 dims, const std::vector<f64>& value, u32 timescale, u32 flags = 1,
                 bool spatial = false) {
    return list("tdbs", tdsb(flags) + tdsn(name) + tdb4(dims, spatial, timescale) + cdat(value));
}
std::string prop_keys(const std::string& name, u32 dims, const std::vector<FKey>& keys, u32 timescale, bool spatial = false) {
    return list("tdbs", tdsb(1) + tdsn(name) + tdb4(dims, spatial, timescale) + keylist(keys, dims, spatial));
}
std::string prop_expr(const std::string& name, u32 dims, const std::vector<f64>& value, u32 timescale, const std::string& src) {
    return list("tdbs", tdsb(1) + tdsn(name) + tdb4(dims, false, timescale) + cdat(value) + chunk("Utf8", src));
}

std::string path(const std::vector<std::pair<std::string, i32>>& steps) {
    std::string b = chunk("tdot", be32(0xFFFFFFFFu)) + chunk("tdpl", be32(static_cast<u32>(steps.size())));
    for (const auto& s : steps) b += list("tdsi", chunk("tdix", be32(static_cast<u32>(s.second))) + tdmn(s.first));
    return list("tdsp", b);
}

/// Preset completo: pares (caminho, nome) e os valores na mesma ordem.
std::string ffx(u32 timescale, const std::vector<std::pair<std::vector<std::pair<std::string, i32>>, std::string>>& paths,
                const std::vector<std::string>& values) {
    std::string beso = be32(1) + be32(1) + be32(0) + be32(timescale);
    beso.resize(56, '\0');
    std::string besc = chunk("beso", beso);
    for (const auto& p : paths) besc += path(p.first) + tdsn(p.second);
    besc += path({{"ADBE End of path sentinel", -1}});
    for (const std::string& v : values) besc += v;
    std::string head = be32(3) + be32(0x5f) + be32(1) + be32(0x01020000);
    std::string body = "FaFX" + chunk("head", head) + list("besc", besc);
    return "RIFX" + be32(static_cast<u32>(body.size())) + body;
}

std::string pard(u8 type, const std::string& name) {
    std::string b(148, '\0');
    b[15] = static_cast<char>(type);
    std::memcpy(&b[16], name.data(), std::min<usize>(name.size(), 31));
    return chunk("pard", b);
}

/// Efeito (LIST sspc): definição dos parâmetros + valores.
std::string effect(const std::string& match, const std::string& name,
                   const std::vector<std::pair<std::string, u8>>& defs, const std::vector<std::pair<u32, std::string>>& values) {
    std::string parT = chunk("parn", be32(static_cast<u32>(defs.size() + 1)));
    parT += tdmn(match + "-0000") + pard(0, "");
    for (usize i = 0; i < defs.size(); ++i) {
        char num[8];
        std::snprintf(num, sizeof num, "-%04u", static_cast<unsigned>(i + 1));
        parT += tdmn(match + num) + pard(defs[i].second, defs[i].first);
    }
    std::string g = tdsb(1) + tdsn(name);
    for (const auto& v : values) {
        char num[8];
        std::snprintf(num, sizeof num, "-%04u", v.first);
        g += tdmn(match + num) + v.second;
    }
    g += tdmn("ADBE Group End");
    std::string fn = "Utf8" + be32(static_cast<u32>(name.size())) + name;
    return list("sspc", chunk("fnam", fn) + list("parT", parT) + list("tdgp", g));
}

struct AeRig {
    Engine e;
    AeRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(1920, 1080, 30.0, nullptr).ok());
    }
    ~AeRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    u64 shape() {
        auto r = e.add_shape(10);
        AUREA_CHECK(r.ok());
        return r.ok() ? *r : 0;
    }
    u64 text(const char* s = "AUREA") {
        auto r = e.add_text(s);
        AUREA_CHECK(r.ok());
        return r.ok() ? *r : 0;
    }
    const Track* track(u64 layer, TrackProperty p, u32 ei = kInvalidIndex, u32 pi = 0) {
        Layer* l = L(layer);
        return l ? l->tracks.find(p, ei, pi) : nullptr;
    }
    void seek(i64 frame) {
        Command s;
        s.type = CommandType::PlaybackSeek;
        s.seek.time = TickNs{frame * 1'000'000'000 / 30 + 1'000};
        AUREA_CHECK(e.apply_command(s).ok());
    }
    bool undo() {
        Command u;
        u.type = CommandType::Undo;
        return e.apply_command(u).ok();
    }
};

bool has_item(const std::vector<ae::SkipItem>& v, const char* needle) {
    for (const ae::SkipItem& s : v)
        if (s.name.find(needle) != std::string::npos || s.reason.find(needle) != std::string::npos) return true;
    return false;
}

void dump(const ae::ImportReport& r) {
    std::printf("\n      %s\n", r.message.c_str());
    for (const ae::SkipItem& s : r.skipped) std::printf("      - [%s] %s: %s (linha %u)\n", s.kind.c_str(), s.name.c_str(), s.reason.c_str(), s.line);
    for (const ae::SkipItem& s : r.notes) std::printf("      ~ [%s] %s: %s (linha %u)\n", s.kind.c_str(), s.name.c_str(), s.reason.c_str(), s.line);
}

u32 param_index(const Engine& e, const char* key, const char* id) {
    const ParameterRegistry* p = const_cast<Engine&>(e).project() ? nullptr : nullptr;
    (void)p;
    EffectRegistry reg;
    register_builtin_effects(reg);
    const ParameterRegistry* specs = reg.params(effect_type_id(key));
    return specs ? specs->find(id) : kInvalidIndex;
}

std::string fixtures_dir() {
    if (const char* env = std::getenv("AUREA_AE_FIXTURES")) return env;
    // Da pasta de testes do motor até a raiz do repositório (worktree ou não).
    for (const char* rel : {"/../../build/reference/ae2026/fixtures", "/../../../../../build/reference/ae2026/fixtures"}) {
        const std::string p = std::string(AUREA_TEST_DATA_DIR) + "/.." + rel;
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) return p;
    }
    return {};
}

/// Lê pelo caminho largo (nomes do AE em pt-BR têm "–" e acentos: no Windows,
/// path::string() lançaria na página de código).
std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string u8name(const std::filesystem::path& p) {
    const auto s = p.filename().u8string();
    return std::string(s.begin(), s.end());
}

} // namespace

// =============================================================================
// Tabela
// =============================================================================

AUREA_TEST(AeImport, TableMatchesRegistry) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    const auto problems = ae::mapping_problems(reg);
    for (const std::string& p : problems) std::printf("\n    %s", p.c_str());
    AUREA_CHECK(problems.empty());
    AUREA_CHECK(ae::table_size() >= 90);
    AUREA_CHECK(std::strcmp(ae::aurea_effect_for("ADBE Gaussian Blur 2"), "aurea.blur.gaussian") == 0);
    AUREA_CHECK(ae::aurea_effect_for("ADBE Calculations") == nullptr);
}

// =============================================================================
// .ffx sintético: efeito + parâmetro parado + keyframes com ease
// =============================================================================

AUREA_TEST(AeImport, SyntheticFfxEffectWithEasedKeys) {
    // Desfoque gaussiano: Blurriness de 0 → 30 em 1 s com ease (velocidade 0,
    // influência 50 % dos dois lados) → 30 → 10 linear; Blur Dimensions = 2
    // (horizontal); Repeat Edge Pixels ligado.
    const u32 ts = 24000;
    const std::string blur = effect("ADBE Gaussian Blur 2", "Desfoque do teste",
        {{"Blurriness", 10}, {"Blur Dimensions", 7}, {"Repeat Edge Pixels", 4}},
        {{1, prop_keys("Blurriness", 1,
                       {FKey{0, {0.0}, 1, 2, {}, {{0.0, 0.5}}},
                        FKey{24000, {30.0}, 2, 1, {{0.0, 0.5}}, {}},
                        FKey{48000, {10.0}, 1, 1, {}, {}}}, ts)},
         {2, prop("Blur Dimensions", 1, {2.0}, ts)},
         {3, prop("Repeat Edge Pixels", 1, {1.0}, ts)}});
    const std::string file = ffx(ts, {{{{"ADBE Effect Parade", -1}, {"ADBE Gaussian Blur 2", 0}}, "Desfoque"}}, {blur});

    ae::FfxDocument doc;
    std::string err;
    AUREA_CHECK(ae::parse_ffx(file, doc, err));
    AUREA_CHECK_EQ(doc.entries.size(), 1u);
    AUREA_CHECK_EQ(doc.timescale, ts);
    if (!doc.entries.empty()) {
        const ae::Node& fx = doc.entries[0].value;
        AUREA_CHECK(fx.kind == ae::Node::Kind::Effect);
        AUREA_CHECK_EQ(fx.params.size(), 4u);
        const ae::Node* b = fx.child("ADBE Gaussian Blur 2-0001");
        AUREA_CHECK(b && b->keys.size() == 3);
        if (b && b->keys.size() == 3) {
            AUREA_CHECK_NEAR(b->keys[1].time, 1.0, 1e-9);
            AUREA_CHECK(b->keys[0].out == ae::KeyInterp::Bezier);
            AUREA_CHECK_NEAR(b->keys[0].outEase[0].influence, 50.0, 1e-9);
        }
    }

    AeRig rig;
    const u64 layer = rig.shape();
    rig.seek(0);
    const ae::ImportReport r = rig.e.import_ae_data(file, "teste.ffx", layer);
    if (!r.ok) dump(r);
    AUREA_CHECK(r.ok);
    AUREA_CHECK_EQ(r.effects, 1u);
    AUREA_CHECK(r.skipped.empty());
    Layer* l = rig.L(layer);
    AUREA_CHECK(l && l->effects.size() == 1);
    if (!l || l->effects.empty()) return;
    const EffectInstance& fx = l->effects.back();
    AUREA_CHECK_EQ(fx.type, effect_type_id("aurea.blur.gaussian"));
    const u32 dimsIdx = param_index(rig.e, "aurea.blur.gaussian", "dimensions");
    const u32 edgeIdx = param_index(rig.e, "aurea.blur.gaussian", "repeat_edges");
    const u32 blurIdx = param_index(rig.e, "aurea.blur.gaussian", "blurriness");
    AUREA_CHECK_NEAR(fx.params[dimsIdx].constant.v[0], 1.0, 1e-6);   // AE 2 (horizontal) → Aurea 1
    AUREA_CHECK_NEAR(fx.params[edgeIdx].constant.v[0], 1.0, 1e-6);
    const Track* t = rig.track(layer, TrackProperty::EffectParam, fx.id, param_track_key(blurIdx, 0));
    AUREA_CHECK(t && t->keys.size() == 3);
    if (!t || t->keys.size() != 3) return;
    AUREA_CHECK_EQ(t->keys[0].time.value, 0);
    AUREA_CHECK_EQ(t->keys[1].time.value, 30);   // 1 s a 30 fps
    AUREA_CHECK_EQ(t->keys[2].time.value, 60);
    AUREA_CHECK(t->keys[0].interp == Interpolation::Bezier);
    AUREA_CHECK_NEAR(t->keys[0].bx1, 0.5, 1e-5);
    AUREA_CHECK_NEAR(t->keys[0].by1, 0.0, 1e-5);
    AUREA_CHECK_NEAR(t->keys[0].bx2, 0.5, 1e-5);
    AUREA_CHECK_NEAR(t->keys[0].by2, 1.0, 1e-5);
    AUREA_CHECK(t->keys[1].interp == Interpolation::Linear);
    AUREA_CHECK_NEAR(t->sample(FrameIndex{15}), 15.0, 1e-3);     // ease simétrico: meio = meio
    AUREA_CHECK_NEAR(t->sample(FrameIndex{45}), 20.0, 1e-3);
    AUREA_CHECK_EQ(r.keyframes, 3u);
}

AUREA_TEST(AeImport, SyntheticFfxSpeedBecomesBezierHandle) {
    // Velocidade de saída 60 u/s com 25 % de influência num trecho de 0 → 100
    // em 2 s: y1 = 60 · 0,25 · 2 / 100 = 0,3 (a conta de sempre do AE).
    const u32 ts = 30720;
    const std::string slider = effect("ADBE Slider Control", "Velocidade", {{"Slider", 10}},
        {{1, prop_keys("Slider", 1, {FKey{0, {0.0}, 1, 2, {}, {{60.0, 0.25}}}, FKey{61440, {100.0}, 2, 1, {{10.0, 0.75}}, {}}}, ts)}});
    const std::string file = ffx(ts, {{{{"ADBE Effect Parade", -1}, {"ADBE Slider Control", 0}}, "Velocidade"}}, {slider});
    AeRig rig;
    const u64 layer = rig.shape();
    rig.seek(0);
    const ae::ImportReport r = rig.e.import_ae_data(file, "vel.ffx", layer);
    AUREA_CHECK(r.ok);
    Layer* l = rig.L(layer);
    if (!l || l->effects.empty()) { AUREA_CHECK(false); return; }
    const Track* t = rig.track(layer, TrackProperty::EffectParam, l->effects.back().id, 0);
    AUREA_CHECK(t && t->keys.size() == 2);
    if (!t || t->keys.size() != 2) return;
    AUREA_CHECK_NEAR(t->keys[0].bx1, 0.25, 1e-6);
    AUREA_CHECK_NEAR(t->keys[0].by1, 0.3, 1e-6);
    AUREA_CHECK_NEAR(t->keys[0].bx2, 0.25, 1e-6);                // 1 − 0,75
    AUREA_CHECK_NEAR(t->keys[0].by2, 1.0 - 10.0 * 0.75 * 2.0 / 100.0, 1e-6);
    AUREA_CHECK_EQ(t->keys[1].time.value, 60);
}

// =============================================================================
// .ffx sintético: animador de texto (intervalo + propriedades + keyframes)
// =============================================================================

AUREA_TEST(AeImport, SyntheticFfxTextAnimator) {
    const u32 ts = 24000;
    // Advanced (Units 1, Based On 3 palavras, Shape 2 rampa sobe)
    const std::string advanced = list("tdgp", tdsb(0) + tdsn("") +
        tdmn("ADBE Text Range Units") + prop("", 1, {1.0}, ts) +
        tdmn("ADBE Text Range Type2") + prop("", 1, {3.0}, ts) +
        tdmn("ADBE Text Selector Mode") + prop("", 1, {1.0}, ts) +
        tdmn("ADBE Text Selector Max Amount") + prop("", 1, {80.0}, ts) +
        tdmn("ADBE Text Range Shape") + prop("", 1, {2.0}, ts) +
        tdmn("ADBE Text Selector Smoothness") + prop("", 1, {100.0}, ts) +
        tdmn("ADBE Text Levels Max Ease") + prop("", 1, {25.0}, ts) +
        tdmn("ADBE Text Levels Min Ease") + prop("", 1, {0.0}, ts) +
        tdmn("ADBE Text Randomize Order") + prop("", 1, {1.0}, ts) +
        tdmn("ADBE Text Random Seed") + prop("", 1, {7.0}, ts) +
        tdmn("ADBE Group End"));
    const std::string range = list("tdgp", tdsb(1) + tdsn("Range Selector 1") +
        tdmn("ADBE Text Percent Start") + prop_keys("", 1, {FKey{0, {0.0}}, FKey{48000, {100.0}}}, ts) +
        tdmn("ADBE Text Percent End") + prop("", 1, {100.0}, ts) +
        tdmn("ADBE Text Percent Offset") + prop("", 1, {0.0}, ts) +
        tdmn("ADBE Text Range Advanced") + advanced +
        tdmn("ADBE Group End"));
    const std::string selectors = list("tdgp", tdsb(1) + tdsn("") + tdmn("ADBE Text Selector") + range + tdmn("ADBE Group End"));
    const std::string props = list("tdgp", tdsb(1) + tdsn("") +
        tdmn("ADBE Text Position 3D") + prop("", 3, {0.0, -120.0, 0.0}, ts, 1, true) +
        tdmn("ADBE Text Scale 3D") + prop("", 3, {50.0, 60.0, 100.0}, ts) +
        tdmn("ADBE Text Opacity") + prop("", 1, {0.0}, ts) +
        tdmn("ADBE Text Fill Color") + prop("", 4, {255.0, 255.0, 128.0, 0.0}, ts) +   // ARGB
        tdmn("ADBE Text Fill Hue") + prop("", 1, {0.0}, ts, 3) +                            // escondida
        tdmn("ADBE Text Tracking Amount") + prop("", 1, {50.0}, ts) +
        tdmn("ADBE Group End"));
    const std::string animator = list("tdgp", tdsb(1) + tdsn("Animador AE") +
        tdmn("ADBE Text Selectors") + selectors + tdmn("ADBE Text Animator Properties") + props + tdmn("ADBE Group End"));
    const std::string more = list("tdgp", tdsb(1) + tdsn("More Options") +
        tdmn("ADBE Text Anchor Point Option") + prop("", 1, {2.0}, ts) + tdmn("ADBE Group End"));
    const std::string file = ffx(ts,
        {{{{"ADBE Text Properties", -1}, {"ADBE Text Animators", -1}, {"ADBE Text Animator", 0}}, "Animador AE"},
         {{{"ADBE Text Properties", -1}, {"ADBE Text More Options", -1}}, "More Options"}},
        {animator, more});

    AeRig rig;
    const u64 layer = rig.text("UM DOIS TRES");
    rig.seek(0);
    const ae::ImportReport r = rig.e.import_ae_data(file, "texto.ffx", layer);
    if (!r.ok || !r.skipped.empty()) dump(r);
    AUREA_CHECK(r.ok);
    AUREA_CHECK_EQ(r.animators, 1u);
    Layer* l = rig.L(layer);
    AUREA_CHECK(l && l->text.animators.size() == 1);
    if (!l || l->text.animators.empty()) return;
    const TextAnimator& a = l->text.animators[0];
    AUREA_CHECK_EQ(a.name, std::string("Animador AE"));
    AUREA_CHECK_EQ(a.selector.type, 2);          // intervalo do AE
    AUREA_CHECK_EQ(a.selector.basedOn, 1);       // palavras
    AUREA_CHECK_EQ(a.selector.shape, 1);         // rampa sobe
    AUREA_CHECK(a.selector.randomOrder);
    AUREA_CHECK_EQ(a.selector.seed, 7u);
    AUREA_CHECK_NEAR(a.selector.amount, 80.0, 1e-6);
    AUREA_CHECK_NEAR(a.selector.easeHigh, 25.0, 1e-6);
    AUREA_CHECK((a.props & kTextPropPosition) && (a.props & kTextPropScale) && (a.props & kTextPropOpacity) &&
                (a.props & kTextPropFill) && (a.props & kTextPropTracking));
    AUREA_CHECK_NEAR(a.position.y, -120.0, 1e-6);
    AUREA_CHECK_NEAR(a.scale.x, 50.0, 1e-6);
    AUREA_CHECK_NEAR(a.scale.y, 60.0, 1e-6);
    AUREA_CHECK_NEAR(a.opacity, 0.0, 1e-6);
    AUREA_CHECK_NEAR(a.fill.x, 1.0, 1e-6);
    AUREA_CHECK_NEAR(a.fill.y, 128.0 / 255.0, 1e-6);
    AUREA_CHECK_NEAR(a.fill.z, 0.0, 1e-6);
    AUREA_CHECK_NEAR(a.tracking, 50.0, 1e-6);
    const Track* em = rig.track(layer, TrackProperty::TextAnimParam, 0, text::kTrackingEm);
    AUREA_CHECK(em && !em->keys.empty() && em->keys[0].value > 0.5f);
    const Track* start = rig.track(layer, TrackProperty::TextAnimParam, 0, text::kSelStart);
    AUREA_CHECK(start && start->keys.size() == 2);
    if (start && start->keys.size() == 2) {
        AUREA_CHECK_EQ(start->keys[1].time.value, 60);
        AUREA_CHECK_NEAR(start->keys[1].value, 100.0, 1e-6);
    }
    const Track* group = rig.track(layer, TrackProperty::TextAnimParam, 0, text::kAnchorGrouping);
    AUREA_CHECK(group && !group->keys.empty() && std::fabs(group->keys[0].value - 1.0f) < 1e-6f);   // palavra
}

AUREA_TEST(AeImport, SyntheticFfxTextAnimatorLinkIsBaked) {
    // O segundo animador liga o Start ao do primeiro por expressão
    // (text.animator(...)), como nos presets da Adobe: vira cópia das keys.
    const u32 ts = 24000;
    auto animator = [&](const char* name, const std::string& start) {
        const std::string range = list("tdgp", tdsb(1) + tdsn("Range Selector 1") +
            tdmn("ADBE Text Percent Start") + start + tdmn("ADBE Group End"));
        const std::string selectors = list("tdgp", tdsb(1) + tdsn("") + tdmn("ADBE Text Selector") + range + tdmn("ADBE Group End"));
        const std::string props = list("tdgp", tdsb(1) + tdsn("") +
            tdmn("ADBE Text Opacity") + prop("", 1, {0.0}, ts) + tdmn("ADBE Group End"));
        return list("tdgp", tdsb(1) + tdsn(name) + tdmn("ADBE Text Selectors") + selectors +
                    tdmn("ADBE Text Animator Properties") + props + tdmn("ADBE Group End"));
    };
    const std::string lead = animator("Lider", prop_keys("", 1, {FKey{0, {0.0}}, FKey{48000, {100.0}}}, ts));
    const std::string follow = animator("Segue", prop_expr("", 1, {0.0}, ts,
        "text.animator(\"Lider\").selector(\"Range Selector 1\").start"));
    const std::string animators = list("tdgp", tdsb(1) + tdsn("") + tdmn("ADBE Text Animator") + lead +
                                               tdmn("ADBE Text Animator") + follow + tdmn("ADBE Group End"));
    const std::string file = ffx(ts, {{{{"ADBE Text Properties", -1}, {"ADBE Text Animators", -1}}, "Animators"}}, {animators});
    AeRig rig;
    const u64 layer = rig.text("LIGADOS");
    rig.seek(0);
    const ae::ImportReport r = rig.e.import_ae_data(file, "link.ffx", layer);
    if (!r.ok || !r.skipped.empty()) dump(r);
    AUREA_CHECK(r.ok);
    AUREA_CHECK(r.skipped.empty());
    AUREA_CHECK_EQ(r.animators, 2u);
    AUREA_CHECK_EQ(r.expressions, 0u);
    AUREA_CHECK(has_item(r.notes, "text.animator"));
    const Track* a = rig.track(layer, TrackProperty::TextAnimParam, 0, text::kSelStart);
    const Track* b = rig.track(layer, TrackProperty::TextAnimParam, 1, text::kSelStart);
    AUREA_CHECK(a && b);
    if (!a || !b) return;
    AUREA_CHECK(!b->expression);
    AUREA_CHECK_EQ(b->keys.size(), a->keys.size());
    for (usize i = 0; i < a->keys.size() && i < b->keys.size(); ++i) {
        AUREA_CHECK_EQ(b->keys[i].time.value, a->keys[i].time.value);
        AUREA_CHECK_NEAR(b->keys[i].value, a->keys[i].value, 1e-6);
    }
}

AUREA_TEST(AeImport, SyntheticFfxUnknownEffectIsReported) {
    const u32 ts = 24000;
    const std::string calc = effect("ADBE Calculations", "Calculations", {{"Input Channel", 7}}, {{1, prop("", 1, {1.0}, ts)}});
    const std::string glow = effect("ADBE Glo2", "Glow", {{"Glow Based On", 7}, {"Glow Threshold", 2}},
                                    {{2, prop("", 1, {127.5}, ts)}});
    const std::string file = ffx(ts,
        {{{{"ADBE Effect Parade", -1}, {"ADBE Calculations", 0}}, "Calc"}, {{{"ADBE Effect Parade", -1}, {"ADBE Glo2", 1}}, "Glow"}},
        {calc, glow});
    AeRig rig;
    const u64 layer = rig.shape();
    const ae::ImportReport r = rig.e.import_ae_data(file, "x.ffx", layer);
    AUREA_CHECK(r.ok);
    AUREA_CHECK_EQ(r.effects, 1u);
    AUREA_CHECK(has_item(r.skipped, "Calculations"));
    Layer* l = rig.L(layer);
    AUREA_CHECK(l && l->effects.size() == 1);
    if (l && !l->effects.empty()) {
        const u32 th = param_index(rig.e, "aurea.light.glow", "threshold");
        AUREA_CHECK_NEAR(l->effects[0].params[th].constant.v[0], 50.0, 1e-3);   // 127,5 de 255 = 50 %
    }
    const std::string json = ae::report_json(r);
    AUREA_CHECK(json.find("\"skipped\":[") != std::string::npos);
    AUREA_CHECK(json.find("Calculations") != std::string::npos);
}

AUREA_TEST(AeImport, SyntheticFfxExpressionRewritesEffectReference) {
    // A propriedade lê um controle que vem DEPOIS no preset (como nos presets
    // do AE): effect("Velocidade")("ADBE Slider Control-0001") vira a
    // referência ao controle do Aurea.
    const u32 ts = 24000;
    const std::string geom = effect("ADBE Geometry2", "Transform", {{"Anchor Point", 6}, {"Position", 6}},
        {{7, prop_expr("Rotation", 1, {0.0}, ts, "effect(\"\tVelocidade\")(\"ADBE Slider Control-0001\") * 2")}});
    const std::string slider = effect("ADBE Slider Control", "Velocidade", {{"Slider", 10}}, {{1, prop("Slider", 1, {45.0}, ts)}});
    const std::string file = ffx(ts,
        {{{{"ADBE Effect Parade", -1}, {"ADBE Geometry2", 0}}, "Transform"},
         {{{"ADBE Effect Parade", -1}, {"ADBE Slider Control", 1}}, "Velocidade"}},
        {geom, slider});
    AeRig rig;
    const u64 layer = rig.shape();
    const ae::ImportReport r = rig.e.import_ae_data(file, "expr.ffx", layer);
    if (!r.ok) dump(r);
    AUREA_CHECK(r.ok);
    AUREA_CHECK_EQ(r.effects, 2u);
    AUREA_CHECK_EQ(r.expressions, 1u);
    Layer* l = rig.L(layer);
    if (!l || l->effects.size() != 2) { AUREA_CHECK(false); return; }
    const u32 rot = param_index(rig.e, "aurea.transform", "rotation");
    Engine::ExpressionInfo info;
    AUREA_CHECK(rig.e.query_expression(layer, static_cast<u32>(TrackProperty::EffectParam), l->effects[0].id,
                                       param_track_key(rot, 0), info));
    AUREA_CHECK(info.exists);
    AUREA_CHECK(info.source.find("aurea.control.slider") != std::string::npos);
    AUREA_CHECK(info.source.find("(\"slider\")") != std::string::npos);
    AUREA_CHECK(info.error.ok);
    AUREA_CHECK_NEAR(info.value, 90.0, 1e-3);
}

AUREA_TEST(AeImport, SyntheticFfxPseudoEffectBecomesOneControlPerParameter) {
    // Efeito de controles do AE (Pseudo/…) com três parâmetros: viram três
    // controles do Aurea, e a expressão escolhe o controle pelo nome do parâmetro.
    const u32 ts = 24000;
    const std::string geom = effect("ADBE Geometry2", "Transform", {{"Anchor Point", 6}, {"Position", 6}},
        {{7, prop_expr("Rotation", 1, {0.0}, ts,
                       "effect(\"Controles\")(\"Amount\") * effect(\"Controles\")(2) * 2")}});
    const std::string controls = effect("Pseudo/aurea_teste", "Controles", {{"Amount", 10}, {"Enable", 4}, {"Tint", 5}},
        {{1, prop("Amount", 1, {30.0}, ts)}, {2, prop("Enable", 1, {1.0}, ts)},
         {3, prop("Tint", 4, {255.0, 255.0, 0.0, 0.0}, ts)}});
    const std::string file = ffx(ts,
        {{{{"ADBE Effect Parade", -1}, {"ADBE Geometry2", 0}}, "Transform"},
         {{{"ADBE Effect Parade", -1}, {"Pseudo/aurea_teste", 1}}, "Controles"}},
        {geom, controls});
    AeRig rig;
    const u64 layer = rig.shape();
    const ae::ImportReport r = rig.e.import_ae_data(file, "pseudo.ffx", layer);
    if (!r.ok || !r.skipped.empty()) dump(r);
    AUREA_CHECK(r.ok);
    AUREA_CHECK(r.skipped.empty());
    AUREA_CHECK_EQ(r.effects, 4u);   // Transform + 3 controles
    Layer* l = rig.L(layer);
    if (!l || l->effects.size() != 4) { AUREA_CHECK(false); return; }
    AUREA_CHECK(l->effects[1].type == effect_type_id("aurea.control.slider"));
    AUREA_CHECK(l->effects[2].type == effect_type_id("aurea.control.checkbox"));
    AUREA_CHECK(l->effects[3].type == effect_type_id("aurea.control.color"));
    AUREA_CHECK_NEAR(l->effects[1].params[0].constant.v[0], 30.0, 1e-4);
    AUREA_CHECK_NEAR(l->effects[3].params[0].constant.v[0], 1.0, 1e-4);   // vermelho (ARGB 255,255,0,0)
    AUREA_CHECK_NEAR(l->effects[3].params[0].constant.v[1], 0.0, 1e-4);
    const u32 rot = param_index(rig.e, "aurea.transform", "rotation");
    Engine::ExpressionInfo info;
    AUREA_CHECK(rig.e.query_expression(layer, static_cast<u32>(TrackProperty::EffectParam), l->effects[0].id,
                                       param_track_key(rot, 0), info));
    AUREA_CHECK(info.source.find("effect(\"aurea.control.slider\")(\"slider\")") != std::string::npos);
    AUREA_CHECK(info.source.find("effect(\"aurea.control.checkbox\")(\"checkbox\")") != std::string::npos);
    if (!info.error.ok) std::printf("\n      %s", info.source.c_str());
    AUREA_CHECK(info.error.ok);
    AUREA_CHECK_NEAR(info.value, 60.0, 1e-3);
}

// =============================================================================
// Desfazer e gravar/reabrir
// =============================================================================

AUREA_TEST(AeImport, UndoRestoresAndProjectRoundTrips) {
    const u32 ts = 24000;
    const std::string blur = effect("ADBE Gaussian Blur 2", "Blur", {{"Blurriness", 10}},
        {{1, prop_keys("", 1, {FKey{0, {0.0}, 1, 2, {}, {{0.0, 0.4}}}, FKey{24000, {12.0}, 2, 1, {{0.0, 0.4}}, {}}}, ts)}});
    const std::string file = ffx(ts, {{{{"ADBE Effect Parade", -1}, {"ADBE Gaussian Blur 2", 0}}, "Blur"}}, {blur});
    AeRig rig;
    const u64 layer = rig.shape();
    const u32 depth = rig.e.history().depth();
    const ae::ImportReport r = rig.e.import_ae_data(file, "undo.ffx", layer);
    AUREA_CHECK(r.ok);
    AUREA_CHECK_EQ(rig.e.history().depth(), depth + 1);
    AUREA_CHECK(rig.L(layer) && rig.L(layer)->effects.size() == 1);

    const std::string path = "ae_import_roundtrip.aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    {
        AeRig other;
        AUREA_CHECK(other.e.load_project(path.c_str()).ok());
        Layer* a = rig.L(layer);
        Layer* b = other.L(layer);
        AUREA_CHECK(a && b);
        if (a && b) {
            AUREA_CHECK_EQ(a->effects.size(), b->effects.size());
            AUREA_CHECK_EQ(a->tracks.size(), b->tracks.size());
            for (u32 i = 0; i < a->tracks.size() && i < b->tracks.size(); ++i) {
                const Track& x = a->tracks.at(i);
                const Track* y = b->tracks.find(x.property, x.effectIndex, x.effectParamIndex);
                AUREA_CHECK(y && y->keys.size() == x.keys.size());
                for (usize k = 0; y && k < x.keys.size() && k < y->keys.size(); ++k) {
                    AUREA_CHECK_EQ(x.keys[k].time.value, y->keys[k].time.value);
                    AUREA_CHECK_NEAR(x.keys[k].value, y->keys[k].value, 1e-6);
                    AUREA_CHECK_NEAR(x.keys[k].bx1, y->keys[k].bx1, 1e-6);
                    AUREA_CHECK_NEAR(x.keys[k].by2, y->keys[k].by2, 1e-6);
                    AUREA_CHECK(x.keys[k].interp == y->keys[k].interp);
                }
            }
        }
    }
    std::remove(path.c_str());

    AUREA_CHECK(rig.undo());
    AUREA_CHECK(rig.L(layer) && rig.L(layer)->effects.empty());
    AUREA_CHECK(rig.track(layer, TrackProperty::EffectParam, 0, 0) == nullptr);
    // Nada aplicável: nenhum passo de desfazer novo.
    const u32 d2 = rig.e.history().depth();
    const std::string none = ffx(ts, {{{{"ADBE Effect Parade", -1}, {"ADBE Calculations", 0}}, "Calc"}},
                                 {effect("ADBE Calculations", "Calc", {}, {})});
    const ae::ImportReport r2 = rig.e.import_ae_data(none, "nada.ffx", layer);
    AUREA_CHECK(!r2.ok);
    AUREA_CHECK_EQ(rig.e.history().depth(), d2);
}

// =============================================================================
// Presets reais do AE (pulam sem a pasta de referência)
// =============================================================================

AUREA_TEST(AeImport, FixturePresetsImport) {
    const std::string dir = fixtures_dir();
    if (dir.empty()) {
        std::printf("(sem build/reference/ae2026/fixtures: pulado) ");
        return;
    }
    u32 files = 0, ok = 0, clean = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const auto e8 = entry.path().extension().u8string();
        const std::string ext(e8.begin(), e8.end());
        if (ext != ".ffx" && ext != ".jsx") continue;
        const std::string data = read_file(entry.path());
        AeRig rig;
        const bool wantsText = data.find("ADBE Text Properties") != std::string::npos || ext == ".jsx";
        const u64 layer = wantsText ? rig.text("AUREA TESTE") : rig.shape();
        const ae::ImportReport r = rig.e.import_ae_data(data, u8name(entry.path()), layer);
        ++files;
        ok += r.ok ? 1 : 0;
        clean += r.ok && r.skipped.empty() ? 1 : 0;
        std::printf("\n    %-48s %s efeitos=%u anim=%u keys=%u expr=%u fora=%zu aprox=%zu",
                    u8name(entry.path()).c_str(), r.ok ? "ok " : "NAO", r.effects, r.animators, r.keyframes,
                    r.expressions, r.skipped.size(), r.notes.size());
        for (const ae::SkipItem& s : r.skipped) std::printf("\n        - [%s] %s: %s", s.kind.c_str(), s.name.c_str(), s.reason.c_str());
        // A camada continua íntegra: o projeto grava.
        AUREA_CHECK(rig.e.save_project("ae_fixture_tmp.aurea").ok());
    }
    std::remove("ae_fixture_tmp.aurea");
    std::printf("\n    %u arquivos, %u importados, %u sem nada de fora\n  ", files, ok, clean);
    AUREA_CHECK(files == 0 || ok > 0);
}

/// Cobertura: com AUREA_AE_PRESETS = pasta Presets do AE instalado.
AUREA_TEST(AeImport, InstalledPresetsCoverage) {
    const char* root = std::getenv("AUREA_AE_PRESETS");
    if (!root) {
        std::printf("(sem AUREA_AE_PRESETS: pulado) ");
        return;
    }
    u32 files = 0, parsed = 0, ok = 0, clean = 0, textFiles = 0, textOk = 0, textClean = 0;
    std::vector<std::pair<std::string, u32>> reasons;
    std::vector<std::pair<std::string, u32>> effects;   ///< matchName → presets que o usam
    // AUREA_AE_COVERAGE_OUT: relatório por preset (TSV em UTF-8) para as notas.
    const char* outPath = std::getenv("AUREA_AE_COVERAGE_OUT");
    std::ofstream out;
    if (outPath) out.open(std::filesystem::path(outPath), std::ios::binary);
    std::vector<std::filesystem::path> paths;
    std::error_code ec;
    // Incremento com error_code: o ++ do for-range lançaria em pastas sem acesso.
    for (std::filesystem::recursive_directory_iterator it(std::filesystem::path(root), ec), end; !ec && it != end;
         it.increment(ec)) {
        if (it->path().extension() == ".ffx") paths.push_back(it->path());
    }
    std::sort(paths.begin(), paths.end());
    for (const std::filesystem::path& p : paths) {
        const std::string data = read_file(p);
        ae::FfxDocument doc;
        std::string err;
        ++files;
        if (!ae::parse_ffx(data, doc, err)) continue;
        ++parsed;
        // Efeitos distintos deste preset (para a frequência por efeito).
        std::vector<std::string> mine;
        auto walk = [&](auto&& self, const ae::Node& n) -> void {
            if (n.kind == ae::Node::Kind::Effect && std::find(mine.begin(), mine.end(), n.match) == mine.end()) mine.push_back(n.match);
            for (const ae::Node& c : n.children) self(self, c);
        };
        for (const ae::Entry& e : doc.entries) walk(walk, e.value);
        for (const std::string& m : mine) {
            auto it = std::find_if(effects.begin(), effects.end(), [&](const auto& x) { return x.first == m; });
            if (it == effects.end()) effects.emplace_back(m, 1); else ++it->second;
        }
        const bool isText = data.find("ADBE Text Properties") != std::string::npos;
        AeRig rig;
        const u64 layer = isText ? rig.text("AUREA TESTE DE TEXTO") : rig.shape();
        const ae::ImportReport r = rig.e.import_ae_data(data, u8name(p), layer);
        ok += r.ok ? 1 : 0;
        const bool c = r.ok && r.skipped.empty();
        clean += c ? 1 : 0;
        if (isText) { ++textFiles; textOk += r.ok ? 1 : 0; textClean += c ? 1 : 0; }
        if (out) {
            const auto rel = std::filesystem::relative(p, std::filesystem::path(root), ec).u8string();
            out << (c ? "completo" : r.ok ? "parcial" : "nao") << '\t' << (isText ? "texto" : "outro") << '\t'
                << std::string(rel.begin(), rel.end()) << '\t' << r.effects << '\t' << r.animators << '\t'
                << r.keyframes << '\t' << r.expressions << '\t';
            for (const ae::SkipItem& s : r.skipped) out << s.kind << ':' << s.name << " (" << s.reason << "); ";
            out << '\n';
        }
        for (const ae::SkipItem& s : r.skipped) {
            const std::string key = s.kind + " | " + (s.kind == "efeito" || s.kind == "parametro" ? s.name : "") + " | " + s.reason;
            auto it = std::find_if(reasons.begin(), reasons.end(), [&](const auto& x) { return x.first == key; });
            if (it == reasons.end()) reasons.emplace_back(key, 1); else ++it->second;
        }
    }
    std::sort(reasons.begin(), reasons.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::printf("\n    presets=%u lidos=%u aplicados=%u completos=%u | texto: %u aplicados=%u completos=%u",
                files, parsed, ok, clean, textFiles, textOk, textClean);
    for (usize i = 0; i < reasons.size() && i < 60; ++i) std::printf("\n      %4u  %s", reasons[i].second, reasons[i].first.c_str());
    // Efeitos por frequência: quantos dos N mais usados têm equivalente.
    std::stable_sort(effects.begin(), effects.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    u32 mapped = 0, mapped60 = 0, uses = 0, usesMapped = 0;
    for (usize i = 0; i < effects.size(); ++i) {
        const char* key = ae::aurea_effect_for(effects[i].first);
        const bool m = key != nullptr;
        mapped += m ? 1 : 0;
        if (i < 60) mapped60 += m ? 1 : 0;
        uses += effects[i].second;
        usesMapped += m ? effects[i].second : 0;
        if (out) out << "#efeito\t" << effects[i].second << '\t' << effects[i].first << '\t' << (key ? key : "-") << '\n';
    }
    std::printf("\n    efeitos distintos=%zu com equivalente=%u | dos 60 mais usados: %u | usos cobertos %u/%u",
                effects.size(), mapped, mapped60, usesMapped, uses);
    std::printf("\n  ");
    AUREA_CHECK(parsed == files);
}

// =============================================================================
// .jsx: DOM do AE no sandbox
// =============================================================================

namespace {
u32 count_layers(AeRig& rig) { return rig.comp()->order().size(); }
Layer* layer_named(AeRig& rig, const char* name) {
    Composition* c = rig.comp();
    for (u32 i = 0; i < c->order().size(); ++i) {
        Layer* l = c->layer(c->order().at(i));
        if (l && l->name == name) return l;
    }
    return nullptr;
}
bool has_line(const std::vector<ae::SkipItem>& v, const char* needle, u32 line) {
    for (const ae::SkipItem& s : v)
        if ((s.name.find(needle) != std::string::npos || s.reason.find(needle) != std::string::npos) && s.line == line) return true;
    return false;
}
} // namespace

AUREA_TEST(AeImport, JsxTextLayerAnimatorKeysEaseAndExpression) {
    const char* src = R"JS(#target aftereffects
app.beginUndoGroup("Titulo");
var comp = app.project.activeItem;
var t = comp.layers.addText("OLA MUNDO");
t.name = "Titulo";
var td = t.property("Source Text").value;
td.fontSize = 120;
td.fillColor = [1, 0.5, 0];
td.justification = ParagraphJustification.CENTER_JUSTIFY;
t.property("Source Text").setValue(td);
var anim = t.property("ADBE Text Properties").property("ADBE Text Animators").addProperty("ADBE Text Animator");
anim.name = "Entrada";
var sel = anim.property("ADBE Text Selectors").addProperty("ADBE Text Selector");
var op = anim.property("ADBE Text Animator Properties").addProperty("ADBE Text Opacity");
op.setValue(0);
var pos = anim.property("ADBE Text Animator Properties").addProperty("ADBE Text Position 3D");
pos.expression = "[10, 20, 0]";
var start = sel.property("ADBE Text Percent Start");
start.setValueAtTime(0, 0);
start.setValueAtTime(1, 100);
var ease = new KeyframeEase(0, 75);
start.setTemporalEaseAtKey(1, [ease], [ease]);
start.setTemporalEaseAtKey(2, [ease], [ease]);
sel.property("ADBE Text Range Advanced").property("ADBE Text Range Shape").setValue(2);
t.transform.position.expression = "wiggle(2, 10)";
t.transform.opacity.setValueAtTime(0, 0);
t.transform.opacity.setValueAtTime(0.5, 100);
t.transform.opacity.setInterpolationTypeAtKey(1, KeyframeInterpolationType.HOLD);
app.endUndoGroup();
)JS";
    AeRig rig;
    const u32 before = count_layers(rig);
    const u32 depth = rig.e.history().depth();
    const ae::ImportReport r = rig.e.import_ae_data(src, "titulo.jsx", 0);
    if (!r.ok || !r.skipped.empty()) dump(r);
    AUREA_CHECK(r.ok);
    AUREA_CHECK_EQ(r.kind, std::string("jsx"));
    AUREA_CHECK_EQ(r.layers, 1u);
    AUREA_CHECK_EQ(r.animators, 1u);
    AUREA_CHECK_EQ(r.expressions, 2u);
    AUREA_CHECK_EQ(count_layers(rig), before + 1);
    {
        // Expressão vetorial no animador: cada componente pega o seu (Y = 20).
        u64 id = 0;
        Composition* c = rig.comp();
        for (u32 i = 0; i < c->order().size(); ++i) {
            const Layer* x = c->layer(c->order().at(i));
            if (x && x->name == "Titulo") id = c->order().at(i).pack();
        }
        Engine::ExpressionInfo info;
        AUREA_CHECK(rig.e.query_expression(id, static_cast<u32>(TrackProperty::TextAnimParam), 0, text::kPosY, info));
        AUREA_CHECK(info.exists && info.error.ok);
        AUREA_CHECK_NEAR(info.value, 20.0, 1e-4);
        AUREA_CHECK(rig.e.query_expression(id, static_cast<u32>(TrackProperty::TextAnimParam), 0, text::kPosX, info));
        AUREA_CHECK_NEAR(info.value, 10.0, 1e-4);
    }
    AUREA_CHECK_EQ(rig.e.history().depth(), depth + 1);   // um passo de desfazer
    Layer* l = layer_named(rig, "Titulo");
    AUREA_CHECK(l != nullptr);
    if (!l) return;
    AUREA_CHECK_EQ(l->text.content, std::string("OLA MUNDO"));
    AUREA_CHECK_NEAR(l->text.size, 120.0, 1e-6);
    AUREA_CHECK_NEAR(l->text.color.y, 0.5, 1e-6);
    AUREA_CHECK_EQ(l->text.alignment, 1u);
    AUREA_CHECK(l->text.animators.size() == 1);
    if (l->text.animators.empty()) return;
    const TextAnimator& a = l->text.animators[0];
    AUREA_CHECK_EQ(a.name, std::string("Entrada"));
    AUREA_CHECK((a.props & kTextPropOpacity) != 0);
    AUREA_CHECK_NEAR(a.opacity, 0.0, 1e-6);
    AUREA_CHECK_EQ(a.selector.shape, 1);
    const Track* st = l->tracks.find(TrackProperty::TextAnimParam, 0, text::kSelStart);
    AUREA_CHECK(st && st->keys.size() == 2);
    if (st && st->keys.size() == 2) {
        AUREA_CHECK_EQ(st->keys[0].time.value, 0);
        AUREA_CHECK_EQ(st->keys[1].time.value, 30);
        AUREA_CHECK(st->keys[0].interp == Interpolation::Bezier);
        AUREA_CHECK_NEAR(st->keys[0].bx1, 0.75, 1e-6);
        AUREA_CHECK_NEAR(st->keys[0].by1, 0.0, 1e-6);
        AUREA_CHECK_NEAR(st->keys[0].bx2, 0.25, 1e-6);
        AUREA_CHECK_NEAR(st->keys[0].by2, 1.0, 1e-6);
    }
    const Track* px = l->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(px && px->expression && px->expression->source == "wiggle(2, 10)");
    const Track* py = l->tracks.find(TrackProperty::PositionY);
    AUREA_CHECK(py && py->expression);
    const Track* opT = l->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(opT && opT->keys.size() == 2);
    if (opT && opT->keys.size() == 2) {
        AUREA_CHECK(opT->keys[0].interp == Interpolation::Hold);
        AUREA_CHECK_NEAR(opT->keys[1].value, 1.0, 1e-6);   // 100 % = 1
        AUREA_CHECK_EQ(opT->keys[1].time.value, 15);
    }
    // Desfazer tira a camada inteira (um passo).
    AUREA_CHECK(rig.undo());
    AUREA_CHECK_EQ(count_layers(rig), before);
    AUREA_CHECK(layer_named(rig, "Titulo") == nullptr);
}

AUREA_TEST(AeImport, JsxEffectsSetValueKeysAndExpressionReference) {
    const char* src = R"JS(var comp = app.project.activeItem;
var l = comp.selectedLayers[0];
var fx = l.property("ADBE Effect Parade").addProperty("ADBE Gaussian Blur 2");
fx.property("ADBE Gaussian Blur 2-0001").setValue(25);
fx.property("Blur Dimensions").setValue(3);
var glow = l.Effects.addProperty("Glow");
glow.property("Glow Radius").setValueAtTime(0, 10);
glow.property("Glow Radius").setValueAtTime(2, 80);
var unknown = l.Effects.addProperty("ADBE Calculations");
unknown.property("ADBE Calculations-0002").setValue(3);
var s = l.Effects.addProperty("ADBE Slider Control");
s.name = "Forca";
s.property("Slider").setValue(7);
l.transform.rotation.expression = 'effect("Forca")("Slider") * 10';
)JS";
    AeRig rig;
    const u64 layer = rig.shape();
    const ae::ImportReport r = rig.e.import_ae_data(src, "fx.jsx", layer);
    if (!r.ok) dump(r);
    AUREA_CHECK(r.ok);
    AUREA_CHECK_EQ(r.effects, 3u);
    AUREA_CHECK(has_line(r.skipped, "addProperty(\"ADBE Calculations\")", 9));
    Layer* l = rig.L(layer);
    AUREA_CHECK(l && l->effects.size() == 3);
    if (!l || l->effects.size() != 3) return;
    const u32 blur = param_index(rig.e, "aurea.blur.gaussian", "blurriness");
    const u32 dims = param_index(rig.e, "aurea.blur.gaussian", "dimensions");
    AUREA_CHECK_NEAR(l->effects[0].params[blur].constant.v[0], 25.0, 1e-6);
    AUREA_CHECK_NEAR(l->effects[0].params[dims].constant.v[0], 2.0, 1e-6);   // AE 3 (vertical) → 2
    const u32 radius = param_index(rig.e, "aurea.light.glow", "radius");
    const Track* gr = rig.track(layer, TrackProperty::EffectParam, l->effects[1].id, param_track_key(radius, 0));
    AUREA_CHECK(gr && gr->keys.size() == 2);
    if (gr && gr->keys.size() == 2) AUREA_CHECK_EQ(gr->keys[1].time.value, 60);
    Engine::ExpressionInfo info;
    AUREA_CHECK(rig.e.query_expression(layer, static_cast<u32>(TrackProperty::RotationZ), kInvalidIndex, 0, info));
    AUREA_CHECK(info.exists && info.error.ok);
    AUREA_CHECK(info.source.find("aurea.control.slider") != std::string::npos);
    AUREA_CHECK_NEAR(info.value, 70.0, 1e-3);
}

AUREA_TEST(AeImport, JsxCoreLanguage) {
    const char* src = R"JS(function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
var arr = [3, 1, 2];
arr.sort(function (a, b) { return a - b; });
var s = "a,b,c".split(",").join("-");
var o = {x: 1, y: [1, 2, 3]};
var total = 0;
for (var k in o) total++;
var counter = (function () { var c = 0; return function () { c += 1; return c; }; })();
counter(); counter();
var w = 0, i = 0;
while (i < 10) { i++; if (i % 2) continue; w += i; }
switch (w) { case 30: w = 31; break; default: w = -1; }
var caught = 0;
try { throw new Error("x"); } catch (e) { caught = e.message === "x" ? 1 : 0; }
var v = fib(10) + arr[0] + s.length + total + Math.max(1, 4) + parseInt("12px") + (typeof o === "object" ? 100 : 0)
      + counter() + w + caught + ("abc".indexOf("c")) + (5).toFixed(1).length;
app.project.activeItem.selectedLayers[0].Effects.addProperty("ADBE Slider Control").property(1).setValue(v);
)JS";
    // 55 + 1 + 5 + 2 + 4 + 12 + 100 + 3 + 31 + 1 + 2 + 3 = 219
    AeRig rig;
    const u64 layer = rig.shape();
    const ae::ImportReport r = rig.e.import_ae_data(src, "core.jsx", layer);
    if (!r.ok) dump(r);
    AUREA_CHECK(r.ok);
    Layer* l = rig.L(layer);
    AUREA_CHECK(l && l->effects.size() == 1);
    if (l && !l->effects.empty()) AUREA_CHECK_NEAR(l->effects[0].params[0].constant.v[0], 219.0, 1e-6);
}

AUREA_TEST(AeImport, JsxSandboxBlocksFileNetworkAndSystem) {
    const char* src = "var f = new File(\"C:/Windows/win.ini\");\n"
                      "f.open(\"r\");\n"
                      "system.callSystem(\"cmd /c echo\");\n"
                      "$.evalFile(\"C:/x.jsx\");\n"
                      "app.executeCommand(2004);\n"
                      "app.project.save(new File(\"C:/out.aep\"));\n"
                      "var s = new Socket();\n"
                      "#include \"outro.jsx\"\n"
                      "app.project.activeItem.selectedLayers[0].Effects.addProperty(\"ADBE Slider Control\").property(1).setValue(5);\n";
    AeRig rig;
    const u64 layer = rig.shape();
    const ae::ImportReport r = rig.e.import_ae_data(src, "sandbox.jsx", layer);
    if (!r.ok) dump(r);
    AUREA_CHECK(r.ok);   // o script continua depois das chamadas bloqueadas
    AUREA_CHECK(has_line(r.skipped, "File()", 1));
    AUREA_CHECK(has_line(r.skipped, "open()", 2));
    AUREA_CHECK(has_line(r.skipped, "system.callSystem()", 3));
    AUREA_CHECK(has_line(r.skipped, "$.evalFile()", 4));
    AUREA_CHECK(has_line(r.skipped, "app.executeCommand()", 5));
    AUREA_CHECK(has_line(r.skipped, "app.project.save()", 6));
    AUREA_CHECK(has_line(r.skipped, "Socket()", 7));
    AUREA_CHECK(has_line(r.skipped, "#include", 8));
    Layer* l = rig.L(layer);
    AUREA_CHECK(l && l->effects.size() == 1);
    const std::string json = ae::report_json(r);
    AUREA_CHECK(json.find("\"line\":3") != std::string::npos);
}

AUREA_TEST(AeImport, JsxInfiniteLoopAndDeepRecursionStop) {
    {
        AeRig rig;
        const ae::ImportReport r = rig.e.import_ae_data("var i = 0;\nwhile (true) { i++; }\n", "loop.jsx", 0);
        AUREA_CHECK(!r.ok);
        AUREA_CHECK(has_item(r.skipped, "passos"));
    }
    {
        AeRig rig;
        const ae::ImportReport r = rig.e.import_ae_data("function f(n) { return f(n + 1) + 1; }\nf(0);\n", "rec.jsx", 0);
        AUREA_CHECK(!r.ok);
        AUREA_CHECK(has_item(r.skipped, "recursão"));
    }
    {
        AeRig rig;
        const ae::ImportReport r = rig.e.import_ae_data(
            "var a = [];\nwhile (true) { a.push(\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\" + a.length); }\n", "mem.jsx", 0);
        AUREA_CHECK(!r.ok);
        AUREA_CHECK(has_item(r.skipped, "memória") || has_item(r.skipped, "passos"));
    }
    {
        AeRig rig;
        const u32 d = rig.e.history().depth();
        const ae::ImportReport r = rig.e.import_ae_data("var x = 1;\nvar y = ;\n", "sintaxe.jsx", 0);
        AUREA_CHECK(!r.ok);
        AUREA_CHECK(has_line(r.skipped, "", 2));
        AUREA_CHECK_EQ(rig.e.history().depth(), d);   // nada mudou: sem passo de desfazer
    }
    {
        // Erro do script com o try/catch dele: pego, o script segue.
        AeRig rig;
        const u64 layer = rig.shape();
        const ae::ImportReport r = rig.e.import_ae_data(
            "var x;\ntry { x.y.z = 1; } catch (e) { }\napp.project.activeItem.selectedLayers[0].Effects.addProperty('ADBE Slider Control');\n",
            "try.jsx", layer);
        AUREA_CHECK(r.ok);
        AUREA_CHECK_EQ(r.effects, 1u);
    }
}

AUREA_TEST(AeImport, JsxProjectSavesAndReopensIdentically) {
    const char* src = R"JS(var comp = app.project.activeItem;
var solid = comp.layers.addSolid([0.2, 0.4, 0.8], "Fundo", 1920, 1080, 1);
solid.transform.scale.setValueAtTime(0, [50, 50]);
solid.transform.scale.setValueAtTime(2, [100, 100]);
solid.transform.scale.setTemporalEaseAtKey(1, [new KeyframeEase(0, 33), new KeyframeEase(0, 33), new KeyframeEase(0, 33)]);
var n = comp.layers.addNull();
n.name = "Controle";
solid.parent = n;
var fx = solid.Effects.addProperty("ADBE Drop Shadow");
fx.property("Opacity").setValue(127.5);
fx.property("Distance").setValueAtTime(0, 0);
fx.property("Distance").setValueAtTime(1, 40);
)JS";
    AeRig rig;
    const ae::ImportReport r = rig.e.import_ae_data(src, "salvar.jsx", 0);
    if (!r.ok || !r.skipped.empty()) dump(r);
    AUREA_CHECK(r.ok);
    AUREA_CHECK_EQ(r.layers, 2u);
    Layer* solid = layer_named(rig, "Fundo");
    Layer* null = layer_named(rig, "Controle");
    AUREA_CHECK(solid && null);
    if (!solid || !null) return;
    AUREA_CHECK(solid->parent.valid());
    AUREA_CHECK_NEAR(solid->shape.fillColor.z, 0.8, 1e-6);
    const Track* sx = solid->tracks.find(TrackProperty::ScaleX);
    AUREA_CHECK(sx && sx->keys.size() == 2 && std::fabs(sx->keys[0].value - 0.5f) < 1e-6f);
    const std::string path = "ae_import_jsx.aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AeRig other;
    AUREA_CHECK(other.e.load_project(path.c_str()).ok());
    Layer* s2 = layer_named(other, "Fundo");
    AUREA_CHECK(s2 != nullptr);
    if (s2) {
        AUREA_CHECK_EQ(s2->effects.size(), solid->effects.size());
        AUREA_CHECK_EQ(s2->tracks.size(), solid->tracks.size());
        for (u32 i = 0; i < solid->tracks.size(); ++i) {
            const Track& a = solid->tracks.at(i);
            const Track* b = s2->tracks.find(a.property, a.effectIndex, a.effectParamIndex);
            AUREA_CHECK(b && b->keys.size() == a.keys.size());
            for (usize k = 0; b && k < a.keys.size() && k < b->keys.size(); ++k) {
                AUREA_CHECK_EQ(a.keys[k].time.value, b->keys[k].time.value);
                AUREA_CHECK_NEAR(a.keys[k].value, b->keys[k].value, 1e-6);
                AUREA_CHECK_NEAR(a.keys[k].bx1, b->keys[k].bx1, 1e-6);
                AUREA_CHECK(a.keys[k].interp == b->keys[k].interp);
            }
        }
        AUREA_CHECK(s2->parent.valid());
    }
    std::remove(path.c_str());
}

// =============================================================================
// 0.3.1: sem camada escolhida, scripts reais (ScriptUI, polyfills, JSXBIN),
// codificações e o relatório
// =============================================================================

namespace {
/// UTF-8 estrito (o que JNI e NSString aceitam).
bool is_valid_utf8(const std::string& s) {
    usize i = 0;
    while (i < s.size()) {
        const u8 c = static_cast<u8>(s[i]);
        if (c < 0x80) { ++i; continue; }
        const usize len = (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + len > s.size() || (len == 2 && c < 0xC2)) return false;
        for (usize k = 1; k < len; ++k) if ((static_cast<u8>(s[i + k]) & 0xC0) != 0x80) return false;
        i += len;
    }
    return true;
}

/// UTF-8 (só BMP) → UTF-16, com ou sem BOM.
std::string utf16(const std::string& u8s, bool littleEndian, bool bom) {
    std::string o;
    auto put = [&](u32 cu) {
        const char lo = static_cast<char>(cu & 0xFF), hi = static_cast<char>(cu >> 8);
        if (littleEndian) { o.push_back(lo); o.push_back(hi); } else { o.push_back(hi); o.push_back(lo); }
    };
    if (bom) put(0xFEFF);
    for (usize i = 0; i < u8s.size();) {
        const u8 c = static_cast<u8>(u8s[i]);
        u32 cp = c;
        usize n = 1;
        if (c >= 0xE0) { cp = c & 0x0F; n = 3; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 2; }
        for (usize k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<u8>(u8s[i + k]) & 0x3F);
        put(cp);
        i += n;
    }
    return o;
}

std::string blur_ffx() {
    const u32 ts = 24000;
    const std::string blur = effect("ADBE Gaussian Blur 2", "Blur", {{"Blurriness", 10}},
        {{1, prop_keys("", 1, {FKey{0, {0.0}}, FKey{24000, {12.0}}}, ts)}});
    return ffx(ts, {{{{"ADBE Effect Parade", -1}, {"ADBE Gaussian Blur 2", 0}}, "Blur"}}, {blur});
}

std::string text_opacity_ffx() {
    const u32 ts = 24000;
    const std::string range = list("tdgp", tdsb(1) + tdsn("Range Selector 1") +
        tdmn("ADBE Text Percent Start") + prop_keys("", 1, {FKey{0, {0.0}}, FKey{24000, {100.0}}}, ts) + tdmn("ADBE Group End"));
    const std::string selectors = list("tdgp", tdsb(1) + tdsn("") + tdmn("ADBE Text Selector") + range + tdmn("ADBE Group End"));
    const std::string props = list("tdgp", tdsb(1) + tdsn("") + tdmn("ADBE Text Opacity") + prop("", 1, {0.0}, ts) + tdmn("ADBE Group End"));
    const std::string animator = list("tdgp", tdsb(1) + tdsn("Surgir") + tdmn("ADBE Text Selectors") + selectors +
                                      tdmn("ADBE Text Animator Properties") + props + tdmn("ADBE Group End"));
    return ffx(ts, {{{{"ADBE Text Properties", -1}, {"ADBE Text Animators", -1}, {"ADBE Text Animator", 0}}, "Surgir"}}, {animator});
}
} // namespace

AUREA_TEST(AeImport, FfxWithoutLayerCreatesOneLikeAe) {
    {
        // Preset de efeito sem camada: sólido do tamanho da composição, com o nome do preset.
        AeRig rig;
        const u32 before = count_layers(rig);
        const u32 depth = rig.e.history().depth();
        const ae::ImportReport r = rig.e.import_ae_data(blur_ffx(), "Desfoque leve.ffx", 0);
        if (!r.ok) dump(r);
        AUREA_CHECK(r.ok);
        AUREA_CHECK_EQ(r.layers, 1u);
        AUREA_CHECK_EQ(r.effects, 1u);
        AUREA_CHECK_EQ(count_layers(rig), before + 1);
        AUREA_CHECK_EQ(rig.e.history().depth(), depth + 1);
        AUREA_CHECK(r.message.find("1 camada criada") != std::string::npos);
        Layer* l = layer_named(rig, "Desfoque leve");
        AUREA_CHECK(l != nullptr);
        if (l) {
            AUREA_CHECK(l->kind == LayerKind::Shape);
            AUREA_CHECK_NEAR(l->shape.bounds.w, 1920.0, 1e-3);
            AUREA_CHECK_NEAR(l->shape.bounds.h, 1080.0, 1e-3);
            AUREA_CHECK_EQ(l->effects.size(), 1u);
            AUREA_CHECK(l->effects.size() == 1 && l->effects[0].type == effect_type_id("aurea.blur.gaussian"));
        }
        AUREA_CHECK(rig.undo());   // um passo desfaz a camada e o preset
        AUREA_CHECK_EQ(count_layers(rig), before);
    }
    {
        // Preset de texto sem camada: camada de texto nova com o animador.
        AeRig rig;
        const u32 before = count_layers(rig);
        const ae::ImportReport r = rig.e.import_ae_data(text_opacity_ffx(), "surgir.ffx", 0);
        if (!r.ok) dump(r);
        AUREA_CHECK(r.ok);
        AUREA_CHECK_EQ(r.layers, 1u);
        AUREA_CHECK_EQ(r.animators, 1u);
        AUREA_CHECK_EQ(count_layers(rig), before + 1);
        Composition* c = rig.comp();
        Layer* t = c->order().size() ? c->layer(c->order().at(c->order().size() - 1)) : nullptr;
        AUREA_CHECK(t && t->kind == LayerKind::Text);
        if (t && t->kind == LayerKind::Text) {
            AUREA_CHECK_EQ(t->text.animators.size(), 1u);
            AUREA_CHECK(!t->text.content.empty() && t->text.content != " ");
        }
    }
    {
        // Nada aplicável: a camada criada sai e não fica passo de desfazer.
        AeRig rig;
        const u32 before = count_layers(rig);
        const u32 depth = rig.e.history().depth();
        const std::string none = ffx(24000, {{{{"ADBE Effect Parade", -1}, {"ADBE Calculations", 0}}, "Calc"}},
                                     {effect("ADBE Calculations", "Calc", {}, {})});
        const ae::ImportReport r = rig.e.import_ae_data(none, "nada.ffx", 0);
        AUREA_CHECK(!r.ok);
        AUREA_CHECK_EQ(r.layers, 0u);
        AUREA_CHECK_EQ(count_layers(rig), before);
        AUREA_CHECK_EQ(rig.e.history().depth(), depth);
    }
    {
        // Preset de texto numa camada que não é de texto: pede uma camada de texto.
        AeRig rig;
        const u64 shape = rig.shape();
        const ae::ImportReport r = rig.e.import_ae_data(text_opacity_ffx(), "surgir.ffx", shape);
        AUREA_CHECK(!r.ok);
        AUREA_CHECK(r.message.find("camada de texto") != std::string::npos);
    }
}

AUREA_TEST(AeImport, JsxScriptUiPanelIsMuteAndReported) {
    const char* panel = R"JS(function buildUI(thisObj) {
    var w = (thisObj instanceof Panel) ? thisObj : new Window("palette", "Meu script", undefined, {resizeable: true});
    var g = w.add("group");
    g.orientation = "row";
    var b = g.add("button", undefined, "Criar");
    b.onClick = function () { app.project.activeItem.layers.addNull(); };
    w.grp.btn.text = "x";
    w.layout.layout(true);
    w.onResizing = w.onResize = function () { this.layout.resize(); };
    if (w instanceof Window) { w.center(); w.show(); }
    return w;
}
var ui = buildUI(this);
)JS";
    {
        AeRig rig;
        const u32 before = count_layers(rig);
        const u32 depth = rig.e.history().depth();
        const ae::ImportReport r = rig.e.import_ae_data(panel, "painel.jsx", 0);
        AUREA_CHECK(!r.ok);   // rodou sem mudar nada: não é "importado"
        AUREA_CHECK(r.skipped.empty());
        AUREA_CHECK(r.message.find("painel com botões") != std::string::npos);
        AUREA_CHECK(has_item(r.notes, "janelas de script"));
        AUREA_CHECK_EQ(count_layers(rig), before);   // o onClick nunca roda
        AUREA_CHECK_EQ(rig.e.history().depth(), depth);
    }
    {
        // Painel que também faz o trabalho direto: a camada entra.
        const std::string src = std::string(panel) +
            "Window.alert(\"pronto\");\n"
            "app.project.activeItem.layers.addSolid([1, 0, 0], \"Vermelho\", 100, 100, 1);\n";
        AeRig rig;
        const ae::ImportReport r = rig.e.import_ae_data(src, "painel2.jsx", 0);
        if (!r.ok) dump(r);
        AUREA_CHECK(r.ok);
        AUREA_CHECK_EQ(r.layers, 1u);
        AUREA_CHECK(layer_named(rig, "Vermelho") != nullptr);
        AUREA_CHECK(has_item(r.notes, "pronto"));
    }
}

AUREA_TEST(AeImport, JsxPolyfillsPrototypesAndJson) {
    const char* src = R"JS(if (!Array.prototype.indexOf) {
    Array.prototype.indexOf = function (x) { for (var i = 0; i < this.length; i++) if (this[i] === x) return i; return -1; };
}
if (!Array.prototype.forEach) {
    Array.prototype.forEach = function (fn) { for (var i = 0; i < this.length; i++) fn(this[i], i); };
}
if (!String.prototype.trim) { String.prototype.trim = function () { return this.replace(/^\s+|\s+$/g, ""); }; }
String.prototype.repete = function (n) { var o = ""; for (var i = 0; i < n; i++) o += this; return o; };
if (typeof Date.prototype.toJSON !== "function") {
    Date.prototype.toJSON = function () { return "data"; };
    String.prototype.toJSON = Number.prototype.toJSON = Boolean.prototype.toJSON = function () { return this.valueOf(); };
}
var rx_one = /^[\],:{}\s]*$/;
var rx_two = /\\(?:["\\\/bfnrt]|u[0-9a-fA-F]{4})/g;
if (typeof JSON !== "object") { JSON = {}; }
var total = 0;
[1, 2, 3].forEach(function (v) { total += v; });
var s = JSON.stringify({a: [1, "b", true, null], c: {d: 2.5}, f: function () {}, u: undefined});
var back = JSON.parse(s);
var hasOwn = Object.prototype.hasOwnProperty.call(back, "c") ? 1 : 0;
var isArr = Object.prototype.toString.call(back.a) === "[object Array]" ? 1 : 0;
var sliced = Array.prototype.slice.call([5, 6, 7], 1).length;
var ok = s === '{"a":[1,"b",true,null],"c":{"d":2.5}}' ? 100 : 0;
var pretty = JSON.stringify([1, {x: "é\n"}], null, 2) === '[\n  1,\n  {\n    "x": "é\\n"\n  }\n]' ? 10000 : 0;
var rep = "ab".repete(3) === "ababab" ? 20000 : 0;
var v = total + ok + back.c.d * 2 + hasOwn + isArr + sliced + [4, 5].indexOf(5) * 1000 + pretty + rep;
app.project.activeItem.selectedLayers[0].Effects.addProperty("ADBE Slider Control").property(1).setValue(v);
)JS";
    // 6 + 100 + 5 + 1 + 1 + 2 + 1000 + 10000 + 20000 = 31115
    AeRig rig;
    const u64 layer = rig.shape();
    const ae::ImportReport r = rig.e.import_ae_data(src, "polyfill.jsx", layer);
    if (!r.ok || !r.skipped.empty()) dump(r);
    AUREA_CHECK(r.ok);
    AUREA_CHECK(r.skipped.empty());   // expressões regulares só declaradas não entram no relatório
    Layer* l = rig.L(layer);
    AUREA_CHECK(l && l->effects.size() == 1);
    if (l && !l->effects.empty()) AUREA_CHECK_NEAR(l->effects[0].params[0].constant.v[0], 31115.0, 1e-6);
    {
        // JSON.parse inválido é um erro que o try/catch do script pega.
        AeRig r2;
        const u64 l2 = r2.shape();
        const ae::ImportReport x = r2.e.import_ae_data(
            "var c = 0;\ntry { JSON.parse('{\"a\":'); } catch (e) { c = 7; }\n"
            "app.project.activeItem.selectedLayers[0].Effects.addProperty('ADBE Slider Control').property(1).setValue(c);\n",
            "parse.jsx", l2);
        AUREA_CHECK(x.ok);
        Layer* ll = r2.L(l2);
        AUREA_CHECK(ll && !ll->effects.empty() && std::fabs(ll->effects[0].params[0].constant.v[0] - 7.0f) < 1e-6f);
    }
}

AUREA_TEST(AeImport, JsxbinIsDetectedByContent) {
    AeRig rig;
    const ae::ImportReport a = rig.e.import_ae_data("@JSXBIN@ES@2.0@MyBbyBn0ABJAnASzBjBBBneBhBffABB0EzACByB", "protegido.jsx", 0);
    AUREA_CHECK_EQ(a.kind, std::string("jsxbin"));
    AUREA_CHECK(!a.ok);
    AUREA_CHECK(a.message.find(".jsxbin") != std::string::npos);
    const ae::ImportReport b = rig.e.import_ae_data(
        "// ferramenta\r\neval(\"@JSXBIN@ES@2.0@MyBbyBn0ABJAnASzBjBBBneBhBffABB0EzACByB\");\r\n", "ferramenta.jsx", 0);
    AUREA_CHECK_EQ(b.kind, std::string("jsxbin"));
    AUREA_CHECK(b.message.find(".jsxbin") != std::string::npos);
}

AUREA_TEST(AeImport, JsxUtf16AndLatin1Sources) {
    const std::string script = "var c = app.project.activeItem;\n"
                               "var t = c.layers.addText(\"Ol\xC3\xA1 a\xC3\xA7\xC3\xA3o\");\n"
                               "t.name = \"T\xC3\xADtulo\";\n";
    for (int mode = 0; mode < 3; ++mode) {
        const std::string data = mode == 0 ? utf16(script, true, true) : mode == 1 ? utf16(script, false, true)
                                                                                  : utf16(script, true, false);
        AeRig rig;
        const ae::ImportReport r = rig.e.import_ae_data(data, "utf16.jsx", 0);
        if (!r.ok) dump(r);
        AUREA_CHECK(r.ok);
        Layer* l = layer_named(rig, "T\xC3\xADtulo");
        AUREA_CHECK(l != nullptr);
        if (l) AUREA_CHECK_EQ(l->text.content, std::string("Ol\xC3\xA1 a\xC3\xA7\xC3\xA3o"));
    }
    {
        // Latin-1 / Windows-1252 (UTF-8 inválido): acentos e aspas tipográficas.
        const std::string latin = "var t = app.project.activeItem.layers.addText(\"\x93" "a\xE7\xE3o\x94\");\n"
                                  "t.name = \"T\xEDtulo\";\n";
        AeRig rig;
        const ae::ImportReport r = rig.e.import_ae_data(latin, "latin1.jsx", 0);
        if (!r.ok) dump(r);
        AUREA_CHECK(r.ok);
        Layer* l = layer_named(rig, "T\xC3\xADtulo");
        AUREA_CHECK(l != nullptr);
        if (l) AUREA_CHECK_EQ(l->text.content, std::string("\xE2\x80\x9C" "a\xC3\xA7\xC3\xA3o\xE2\x80\x9D"));
    }
    {
        // Continuação de linha com CRLF dentro do texto.
        AeRig rig;
        const u64 layer = rig.shape();
        const ae::ImportReport r = rig.e.import_ae_data(
            "var s = \"abc\\\r\ndef\";\r\napp.project.activeItem.selectedLayers[0].Effects.addProperty('ADBE Slider Control').property(1).setValue(s.length);\r\n",
            "crlf.jsx", layer);
        if (!r.ok) dump(r);
        AUREA_CHECK(r.ok);
        Layer* l = rig.L(layer);
        AUREA_CHECK(l && !l->effects.empty() && std::fabs(l->effects[0].params[0].constant.v[0] - 6.0f) < 1e-6f);
    }
}

AUREA_TEST(AeImport, ReportJsonIsValidUtf8AndScriptErrorsKeepCounts) {
    {
        ae::ImportReport r;
        r.kind = "jsx";
        r.message = "quebrado \xC3";
        r.skip("chamada", "\xFF\xFEnome", "ok \xE2\x82");
        const std::string json = ae::report_json(r);
        AUREA_CHECK(is_valid_utf8(json));
        AUREA_CHECK(json.find("\"changes\":0") != std::string::npos);
    }
    {
        // alert longo com acentos: o corte em 200 bytes não parte um caractere.
        std::string longText;
        for (int i = 0; i < 150; ++i) longText += "\xC3\xA9";
        AeRig rig;
        const ae::ImportReport r = rig.e.import_ae_data("alert(\"x" + longText + "\");\napp.project.activeItem.layers.addNull();\n", "alert.jsx", 0);
        AUREA_CHECK(r.ok);
        AUREA_CHECK(!r.notes.empty());
        for (const ae::SkipItem& n : r.notes) AUREA_CHECK(is_valid_utf8(n.reason));
        AUREA_CHECK(is_valid_utf8(ae::report_json(r)));
    }
    {
        // Erro DEPOIS de mudar o projeto: o que entrou fica, com as contagens e o erro.
        AeRig rig;
        const u32 before = count_layers(rig);
        const u32 depth = rig.e.history().depth();
        const ae::ImportReport r = rig.e.import_ae_data(
            "var n = app.project.activeItem.selectedLayers.length;\n"
            "app.project.activeItem.layers.addNull().name = \"sel\" + n;\n"
            "naoExiste();\n", "erro.jsx", 0);
        AUREA_CHECK(!r.ok);
        AUREA_CHECK_EQ(r.layers, 1u);
        AUREA_CHECK(r.changes > 0);
        AUREA_CHECK(r.message.find("linha 3") != std::string::npos);
        AUREA_CHECK(r.message.find("1 camada criada") != std::string::npos);
        AUREA_CHECK(layer_named(rig, "sel0") != nullptr);   // sem camada escolhida: selectedLayers = []
        AUREA_CHECK_EQ(count_layers(rig), before + 1);
        AUREA_CHECK_EQ(rig.e.history().depth(), depth + 1);
        const std::string json = ae::report_json(r);
        AUREA_CHECK(json.find("\"ok\":false") != std::string::npos && json.find("\"layers\":1") != std::string::npos);
    }
    {
        // Rodou sem mudar nada: não sai como "importado".
        AeRig rig;
        const ae::ImportReport r = rig.e.import_ae_data("var x = 1 + 1;\n", "nada.jsx", 0);
        AUREA_CHECK(!r.ok);
        AUREA_CHECK(r.message.find("não mudou nada") != std::string::npos);
    }
}
