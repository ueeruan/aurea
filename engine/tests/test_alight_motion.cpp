// =============================================================================
//  Importação de efeitos do Alight Motion (project/AlightMotion.hpp): XML de
//  cena no formato real (a mesma forma da cena de teste do importador antigo),
//  tabela de conversão, keyframes normalizados, faixas, pacote zip e o preset
//  resultante aplicado de verdade pelo motor.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/project/AlightMotion.hpp"
#include "aurea/project/Presets.hpp"

#include <string>
#include <vector>

using namespace aurea;
using presets::AlightImportReport;

namespace {

/// Uma cena com duas camadas com efeito: a primeira (Fundo) tem desfoque,
/// um efeito sem equivalente (mosaico), brilho com keyframes e motion blur;
/// a segunda (Titulo) tem tremor — fica de fora (vale a primeira camada).
const char* const kCena = R"(<?xml version='1.0' encoding='UTF-8' ?>
<scene title="Brilho AM" width="1920" height="1080" bgcolor="#ff101418" totalTime="4000" fps="30">
  <media uri="content://fotos/1" filename="foto.png" type="image/png" />
  <shape id="1" label="Fundo" startTime="0" endTime="2000" fillType="color" s=".rect">
    <transform>
      <location value="960.000000,540.000000,0.000000" />
      <scale value="1.500000,1.500000" />
    </transform>
    <fillColor value="#ff2a2a2a" />
    <property name="size" type="vec2" value="800.000000,600.000000" />
    <effect id="com.alightcreative.effects.gaussianblur2" locallyApplied="true">
      <property name="strength" type="float" value="0.250000" />
      <property name="quality" type="int" value="2" />
    </effect>
    <effect id="com.alightcreative.effects.mosaic" locallyApplied="true">
      <property name="size" type="float" value="12.000000" />
    </effect>
    <effect id="com.alightcreative.effects.glow2" locallyApplied="true">
      <property name="radius" type="float" value="40.000000" />
      <property name="intensity" type="float">
        <kf t="0.000000" v="0.000000" />
        <kf t="0.500000" v="1.500000" e="cubicBezier 0.25 0.1 0.25 1.0" />
        <kf t="1.000000" v="0.500000" e="hold" />
      </property>
      <property name="threshold" type="float" value="0.400000" />
      <property name="color" type="color" value="#ffff8800" />
      <property name="sparkle" type="float" value="0.300000" />
    </effect>
    <effect id="com.alightcreative.effects.motionblur4" locallyApplied="true" />
  </shape>
  <text id="2" label="Titulo" startTime="500" endTime="2500" fillType="color" size="48.000000">
    <transform><location value="100.000000,200.000000,0.000000" /></transform>
    <content>Ola &amp; bem-vindo</content>
    <effect id="com.alightcreative.effects.shake" locallyApplied="true">
      <property name="strength" type="float" value="0.200000" />
    </effect>
  </text>
</scene>
)";

struct AmRig {
    Engine e;
    AmRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    }
    ~AmRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    void range(u64 id, i64 s, i64 en, i64 offset) {
        Command cmd;
        cmd.type = CommandType::LayerSetTimeRange;
        cmd.layer_range.layer = LayerId::unpack(id);
        cmd.layer_range.start = FrameIndex{s};
        cmd.layer_range.end = FrameIndex{en};
        cmd.layer_range.offset = FrameIndex{offset};
        cmd.layer_range.setOffset = true;
        AUREA_CHECK(e.apply_command(cmd).ok());
    }
};

bool has(const std::vector<std::string>& v, const std::string& needle) {
    for (const std::string& s : v) if (s.find(needle) != std::string::npos) return true;
    return false;
}

u32 param_of(const EffectRegistry& reg, const char* key, const char* id) {
    const ParameterRegistry* specs = reg.params(effect_type_id(key));
    return specs ? specs->find(id) : kInvalidIndex;
}

const Track* track_of(const std::vector<Track>& ts, u32 effect, u32 paramKey) {
    for (const Track& t : ts) if (t.effectIndex == effect && t.effectParamIndex == paramKey) return &t;
    return nullptr;
}

// --- zip mínimo (para o teste do pacote) -------------------------------------
void put16(std::string& s, u32 v) { s.push_back(static_cast<char>(v & 0xFF)); s.push_back(static_cast<char>((v >> 8) & 0xFF)); }
void put32(std::string& s, u32 v) { put16(s, v & 0xFFFF); put16(s, v >> 16); }

struct ZipEntry {
    std::string name;
    std::string data;
    bool deflate = false;   ///< método 8 com um bloco "stored" do deflate
};

std::string make_zip(const std::vector<ZipEntry>& entries) {
    std::string out, cd;
    for (const ZipEntry& z : entries) {
        std::string payload = z.data;
        u32 method = 0;
        if (z.deflate) {
            payload.clear();
            payload.push_back('\x01');   // BFINAL = 1, BTYPE = 00 (sem compressão)
            put16(payload, static_cast<u32>(z.data.size()));
            put16(payload, ~static_cast<u32>(z.data.size()) & 0xFFFF);
            payload += z.data;
            method = 8;
        }
        const u32 off = static_cast<u32>(out.size());
        put32(out, 0x04034b50u); put16(out, 20); put16(out, 0); put16(out, method); put16(out, 0); put16(out, 0);
        put32(out, 0); put32(out, static_cast<u32>(payload.size())); put32(out, static_cast<u32>(z.data.size()));
        put16(out, static_cast<u32>(z.name.size())); put16(out, 0);
        out += z.name;
        out += payload;
        put32(cd, 0x02014b50u); put16(cd, 20); put16(cd, 20); put16(cd, 0); put16(cd, method); put16(cd, 0); put16(cd, 0);
        put32(cd, 0); put32(cd, static_cast<u32>(payload.size())); put32(cd, static_cast<u32>(z.data.size()));
        put16(cd, static_cast<u32>(z.name.size())); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0); put32(cd, 0); put32(cd, off);
        cd += z.name;
    }
    const u32 cdOff = static_cast<u32>(out.size());
    out += cd;
    put32(out, 0x06054b50u); put16(out, 0); put16(out, 0);
    put16(out, static_cast<u32>(entries.size())); put16(out, static_cast<u32>(entries.size()));
    put32(out, static_cast<u32>(cd.size())); put32(out, cdOff); put16(out, 0);
    return out;
}

} // namespace

AUREA_TEST(AlightMotion, MappingTableMatchesTheRegistry) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    const auto problems = presets::alight_mapping_problems(reg);
    for (const std::string& p : problems) AUREA_CHECK_MSG(false, p.c_str());
    AUREA_CHECK(problems.empty());
}

AUREA_TEST(AlightMotion, SceneEffectsBecomeAnEffectsPreset) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    AlightImportReport rep;
    const std::string js = presets::import_alight_motion(kCena, &reg, rep);
    AUREA_CHECK_MSG(!js.empty(), rep.error.c_str());
    AUREA_CHECK(rep.error.empty());
    AUREA_CHECK_EQ(rep.mapped, 2u);
    AUREA_CHECK(rep.layer == "Fundo");
    AUREA_CHECK(rep.name == "Brilho AM");
    AUREA_CHECK(has(rep.skipped, "com.alightcreative.effects.mosaic"));
    AUREA_CHECK(has(rep.skipped, "com.alightcreative.effects.motionblur4"));
    AUREA_CHECK_EQ(rep.skipped.size(), usize{2});
    AUREA_CHECK(has(rep.warnings, "sparkle"));          // parâmetro sem equivalente: aviso, não erro
    AUREA_CHECK(has(rep.warnings, "Fundo"));            // a outra camada com efeito ficou de fora
    AUREA_CHECK(!has(rep.warnings, "quality"));         // parâmetro sabidamente irrelevante: sem ruído

    presets::Preset p;
    std::string err;
    AUREA_CHECK_MSG(presets::parse(js, p, &reg, &err), err.c_str());
    AUREA_CHECK(p.kind == presets::PresetKind::Effects && p.name == "Brilho AM");
    AUREA_CHECK_NEAR(static_cast<f32>(p.fps), 30.0f, 1e-6f);
    AUREA_CHECK_EQ(p.effects.size(), usize{2});
    if (p.effects.size() != 2) return;
    AUREA_CHECK(p.effects[0].type == effect_type_id(effect_keys::kGaussianBlur));
    AUREA_CHECK(p.effects[1].type == effect_type_id(effect_keys::kGlow));
    // strength 0.25 (fração) → 25 px.
    const u32 blurriness = param_of(reg, effect_keys::kGaussianBlur, "blurriness");
    AUREA_CHECK_NEAR(p.effects[0].params[blurriness].constant.v[0], 25.0f, 1e-4f);
    const u32 radius = param_of(reg, effect_keys::kGlow, "radius");
    const u32 threshold = param_of(reg, effect_keys::kGlow, "threshold");
    const u32 intensity = param_of(reg, effect_keys::kGlow, "intensity");
    const u32 color = param_of(reg, effect_keys::kGlow, "color");
    const EffectInstance& glow = p.effects[1];
    AUREA_CHECK_NEAR(glow.params[radius].constant.v[0], 40.0f, 1e-4f);      // já em px
    AUREA_CHECK_NEAR(glow.params[threshold].constant.v[0], 40.0f, 1e-4f);   // 0.4 → 40 %
    // #ffff8800 (sRGB) → linear.
    AUREA_CHECK_NEAR(glow.params[color].constant.v[0], 1.0f, 1e-5f);
    AUREA_CHECK_NEAR(glow.params[color].constant.v[1], Color::srgb_to_linear(136.0f / 255.0f), 1e-5f);
    AUREA_CHECK_NEAR(glow.params[color].constant.v[2], 0.0f, 1e-6f);
    AUREA_CHECK_NEAR(glow.params[color].constant.v[3], 1.0f, 1e-6f);
    // Intensidade animada: t 0 / 0.5 / 1 numa camada de 2 s a 30 fps.
    AUREA_CHECK_EQ(p.effectTracks.size(), usize{1});
    const Track* t = track_of(p.effectTracks, 1, param_track_key(intensity, 0));
    AUREA_CHECK(t != nullptr);
    if (!t) return;
    AUREA_CHECK_EQ(t->keys.size(), usize{3});
    if (t->keys.size() != 3) return;
    AUREA_CHECK_EQ(t->keys[0].time.value, i64{0});
    AUREA_CHECK_EQ(t->keys[1].time.value, i64{30});
    AUREA_CHECK_EQ(t->keys[2].time.value, i64{60});
    AUREA_CHECK_NEAR(t->keys[0].value, 0.0f, 1e-6f);
    AUREA_CHECK_NEAR(t->keys[1].value, 1.5f, 1e-6f);
    AUREA_CHECK_NEAR(t->keys[2].value, 0.5f, 1e-6f);
    // A curva do AM é a CHEGADA: a do segundo keyframe sai do primeiro.
    AUREA_CHECK(t->keys[0].interp == Interpolation::Bezier);
    AUREA_CHECK_NEAR(t->keys[0].bx1, 0.25f, 1e-6f);
    AUREA_CHECK_NEAR(t->keys[0].by1, 0.1f, 1e-6f);
    AUREA_CHECK_NEAR(t->keys[0].bx2, 0.25f, 1e-6f);
    AUREA_CHECK_NEAR(t->keys[0].by2, 1.0f, 1e-6f);
    AUREA_CHECK(t->keys[1].interp == Interpolation::Hold);

    // Envelope das pontes: JSON válido com o preset dentro.
    const std::string env = presets::alight_import_envelope(js, rep);
    json::Value v;
    AUREA_CHECK(json::parse(env, v));
    AUREA_CHECK(v.get("preset") && v.get("preset")->string == js);
    AUREA_CHECK(v.get("mapped") && v.get("mapped")->number == 2.0);
    AUREA_CHECK(v.get("skipped") && v.get("skipped")->array.size() == 2);
    AUREA_CHECK(v.get("warnings") && !v.get("warnings")->array.empty());
    AUREA_CHECK(v.get("error") && v.get("error")->string.empty());
}

AUREA_TEST(AlightMotion, ImportedPresetAppliesThroughTheEngine) {
    AmRig r;
    const u64 b = *r.e.add_shape(0);
    r.range(b, 40, 120, 5);
    AlightImportReport rep;
    const std::string js = r.e.import_alight_motion(kCena, rep);
    AUREA_CHECK_MSG(!js.empty(), rep.error.c_str());
    std::string err;
    AUREA_CHECK_MSG(r.e.apply_preset(b, js, 0, &err), err.c_str());
    const Layer* lb = r.L(b);
    AUREA_CHECK_EQ(lb->effects.size(), usize{2});
    if (lb->effects.size() != 2) return;
    const EffectRegistry& reg = r.e.effects();
    AUREA_CHECK(lb->effects[0].type == effect_type_id(effect_keys::kGaussianBlur));
    AUREA_CHECK(lb->effects[1].type == effect_type_id(effect_keys::kGlow));
    AUREA_CHECK_NEAR(lb->effects[0].params[param_of(reg, effect_keys::kGaussianBlur, "blurriness")].constant.v[0], 25.0f, 1e-4f);
    AUREA_CHECK_NEAR(lb->effects[1].params[param_of(reg, effect_keys::kGlow, "threshold")].constant.v[0], 40.0f, 1e-4f);
    // Keyframe do meio (t = 0.5 → 30 quadros) cai 30 quadros depois do início da camada.
    const u32 intensity = param_of(reg, effect_keys::kGlow, "intensity");
    const Track* t = lb->tracks.find(TrackProperty::EffectParam, lb->effects[1].id, param_track_key(intensity, 0));
    AUREA_CHECK(t && t->keys.size() == 3);
    if (t && t->keys.size() == 3) {
        AUREA_CHECK_EQ(lb->timeline_time(t->keys[0].time).value, i64{40});
        AUREA_CHECK_EQ(lb->timeline_time(t->keys[1].time).value, i64{70});
        AUREA_CHECK_NEAR(t->sample(t->keys[1].time), 1.5f, 1e-5f);
    }
    // Nenhum keyframe do desfoque (ele era fixo).
    AUREA_CHECK(lb->tracks.find(TrackProperty::EffectParam, lb->effects[0].id, param_track_key(0, 0)) == nullptr);
}

AUREA_TEST(AlightMotion, OutOfRangeIsClampedAndUntimedKeysBecomeStatic) {
    // Sem startTime/endTime nem totalTime: a duração da camada é desconhecida.
    const char* xml = R"(<scene width="1080" height="1080" fps="60">
  <shape id="1" label="Sem tempo" s=".rect">
    <effect id="com.alightcreative.effects.gaussianblur">
      <property name="radius" type="float" value="900" />
    </effect>
    <effect id="com.alightcreative.effects.glow">
      <property name="threshold" type="float" value="-0.5" />
      <property name="intensity" type="float">
        <kf t="0.0" v="0.25" />
        <kf t="1.0" v="0.75" e="easeInOut" />
      </property>
    </effect>
  </shape>
</scene>)";
    EffectRegistry reg;
    register_builtin_effects(reg);
    AlightImportReport rep;
    const std::string js = presets::import_alight_motion(xml, &reg, rep);
    AUREA_CHECK_MSG(!js.empty(), rep.error.c_str());
    presets::Preset p;
    AUREA_CHECK(presets::parse(js, p, &reg));
    AUREA_CHECK_EQ(p.effects.size(), usize{2});
    if (p.effects.size() != 2) return;
    AUREA_CHECK_NEAR(static_cast<f32>(p.fps), 60.0f, 1e-6f);
    AUREA_CHECK_NEAR(p.effects[0].params[param_of(reg, effect_keys::kGaussianBlur, "blurriness")].constant.v[0], 500.0f, 1e-4f);
    AUREA_CHECK_NEAR(p.effects[1].params[param_of(reg, effect_keys::kGlow, "threshold")].constant.v[0], 0.0f, 1e-6f);
    // Parado no valor de t = 0 (0.25 como fração → 0.5 de intensidade).
    AUREA_CHECK_NEAR(p.effects[1].params[param_of(reg, effect_keys::kGlow, "intensity")].constant.v[0], 0.5f, 1e-6f);
    AUREA_CHECK(p.effectTracks.empty());
    AUREA_CHECK(has(rep.warnings, "fora da faixa"));
    AUREA_CHECK(has(rep.warnings, "valor fixo"));
    AUREA_CHECK(rep.skipped.empty());
}

AUREA_TEST(AlightMotion, FadeBecomesTransformOpacityKeys) {
    const char* xml = R"(<scene width="1920" height="1080" fps="30" totalTime="2000">
  <text id="7" label="Titulo" startTime="0" endTime="2000">
    <effect id="com.alightcreative.effects.fade" locallyApplied="true">
      <property name="inTime" type="float" value="0.500000" />
      <property name="outTime" type="float" value="0.500000" />
    </effect>
  </text>
</scene>)";
    EffectRegistry reg;
    register_builtin_effects(reg);
    AlightImportReport rep;
    const std::string js = presets::import_alight_motion(xml, &reg, rep);
    AUREA_CHECK_MSG(!js.empty(), rep.error.c_str());
    presets::Preset p;
    AUREA_CHECK(presets::parse(js, p, &reg));
    AUREA_CHECK_EQ(p.effects.size(), usize{1});
    if (p.effects.empty()) return;
    AUREA_CHECK(p.effects[0].type == effect_type_id(effect_keys::kTransform));
    const u32 opacity = param_of(reg, effect_keys::kTransform, "opacity");
    const Track* t = track_of(p.effectTracks, 0, param_track_key(opacity, 0));
    AUREA_CHECK(t && t->keys.size() == 4);
    if (!t || t->keys.size() != 4) return;
    const i64 times[4] = {0, 15, 45, 60};
    const f32 values[4] = {0.0f, 100.0f, 100.0f, 0.0f};
    for (u32 i = 0; i < 4; ++i) {
        AUREA_CHECK_EQ(t->keys[i].time.value, times[i]);
        AUREA_CHECK_NEAR(t->keys[i].value, values[i], 1e-6f);
    }
}

AUREA_TEST(AlightMotion, MalformedOrUselessInputFailsCleanly) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    std::string deep;
    for (int i = 0; i < 5000; ++i) deep += "<a>";
    std::string junk = "PK\x03\x04";
    junk += std::string(64, '\x7f');
    const std::string cases[] = {
        "",
        "isso nao e xml",
        "<coisa/>",
        "<scene width=\"10\" height=\"10\"><shape label=\"x\"/></scene>",   // cena sem efeito
        "<scene",                                                             // truncado
        deep,                                                                 // aninhado demais
        junk,                                                                 // zip quebrado
        make_zip({ZipEntry{"leia.txt", "nada aqui", false}}),                // zip sem cena
        std::string("\x00\x01\x02\xff\xfe", 5),
    };
    for (const std::string& c : cases) {
        AlightImportReport rep;
        const std::string js = presets::import_alight_motion(c, &reg, rep);
        AUREA_CHECK_MSG(js.empty(), c.substr(0, 40).c_str());
        AUREA_CHECK_MSG(!rep.error.empty(), c.substr(0, 40).c_str());
    }
    // Só efeitos sem equivalente: erro, com a lista do que ficou de fora.
    AlightImportReport rep;
    const std::string none = presets::import_alight_motion(
        R"(<scene fps="30"><shape label="v"><effect id="com.alightcreative.effects.vignette"/><effect id="com.alightcreative.effects.curves"/></shape></scene>)",
        &reg, rep);
    AUREA_CHECK(none.empty() && !rep.error.empty());
    AUREA_CHECK(has(rep.skipped, "vignette") && has(rep.skipped, "curves"));
    // Registro nulo = efeitos embutidos.
    AlightImportReport rep2;
    AUREA_CHECK(!presets::import_alight_motion(kCena, nullptr, rep2).empty());
}

AUREA_TEST(AlightMotion, ZipPackageUsesTheLargestScene) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    const std::string thumb = R"(<scene title="miniatura"><shape label="m"><effect id="com.alightcreative.effects.sharpen"/></shape></scene>)";
    for (const bool deflate : {false, true}) {
        const std::string zip = make_zip({
            ZipEntry{"thumbs/elemento.xml", thumb, deflate},
            ZipEntry{"media/foto.png", std::string("\x89PNG....", 8), false},
            ZipEntry{"projeto/cena.xml", kCena, deflate},
        });
        AlightImportReport rep;
        const std::string js = presets::import_alight_motion(zip, &reg, rep);
        AUREA_CHECK_MSG(!js.empty(), rep.error.c_str());
        AUREA_CHECK(rep.name == "Brilho AM" && rep.mapped == 2);
        presets::Preset p;
        AUREA_CHECK(presets::parse(js, p, &reg) && p.effects.size() == 2);
    }
}
