// =============================================================================
//  Plugin V3, fase 0: o projeto sabe quais plugins usa e os leva junto.
// =============================================================================
#include "TestFramework.hpp"

#include "../src/plugins/PluginInternal.hpp"
#include "aurea/Engine.hpp"
#include "aurea/plugins/PluginManager.hpp"
#include "aurea/project/ProjectPackage.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::test;
namespace fs = std::filesystem;

namespace {
fs::path fresh_dir(const char* name) {
    const fs::path dir = fs::path(std::getenv("TEMP") ? std::getenv("TEMP") : ".") / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

void write_text(const fs::path& path, const char* text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary) << text;
}

struct Rig {
    Engine e;
    Rig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    }
    ~Rig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    void effect(u32 type) {
        Layer* l = comp()->layer(comp()->add_layer(LayerKind::Text, "S"));
        EffectInstance inst;
        inst.type = type;
        l->effects.push_back(inst);
    }
};
} // namespace

AUREA_TEST(PluginV3, MissingPluginStaysKnownAcrossSaveAndUnusedOnesLeave) {
    const fs::path dir = fresh_dir("aurea_plugin_v3_refs");
    const std::string path = (dir / "p.aurea").string();
    {
        Rig r;
        r.effect(0xABCD1234u);
        r.e.project()->set_plugin_refs({{"com.x.used", "1.2.3", 0xABCD1234u}, {"com.x.gone", "1.0.0", 0x11112222u}});
        AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    }
    std::vector<PluginRef> refs;
    AUREA_CHECK(package::list_plugins(path, refs).ok());
    AUREA_CHECK_EQ(refs.size(), 1u);
    if (!refs.empty()) {
        AUREA_CHECK(refs[0].id == "com.x.used");
        AUREA_CHECK(refs[0].version == "1.2.3");
        AUREA_CHECK_EQ(refs[0].typeId, 0xABCD1234u);
    }
    Rig reopened;
    AUREA_CHECK(reopened.e.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(reopened.e.project()->plugin_refs().size(), 1u);
}

AUREA_TEST(PluginV3, PackageCarriesThePluginWithoutItsShaderCache) {
    const fs::path dir = fresh_dir("aurea_plugin_v3_pkg");
    const std::string aurea = (dir / "p.aurea").string(), pkg = (dir / "p.aureaproj").string();
    {
        Rig r;
        AUREA_CHECK(r.e.save_project(aurea.c_str()).ok());
    }
    const fs::path plugin = dir / "installed" / "com.x.glow";
    write_text(plugin / "plugin.json", "{}");
    write_text(plugin / "shaders" / "a.frag", "void main(){}");
    write_text(plugin / ".cache" / "passe0.spv", "lixo");
    std::vector<package::PluginFile> plugins{{"com.x.glow", "2.0.1", plugin.string()}, {"com.x.absent", "1.0.0", (dir / "nada").string()}};
    package::ExportResult written;
    AUREA_CHECK(package::write_package(aurea, pkg, "T", "1", {}, &written, plugins).ok());

    const std::string out = (dir / "volta.aurea").string(), media = (dir / "midia").string();
    package::ImportResult imported;
    AUREA_CHECK(package::read_package(pkg, out, media, imported).ok());
    AUREA_CHECK(!imported.plugins.empty());
    if (imported.plugins.empty()) return;
    AUREA_CHECK(imported.plugins[0].id == "com.x.glow");
    AUREA_CHECK(imported.plugins[0].version == "2.0.1");
    std::vector<std::string> files;
    AUREA_CHECK(package::extract_model_archive(imported.plugins[0].directory, (dir / "aberto").string(), files).ok());
    AUREA_CHECK(fs::exists(dir / "aberto" / "plugin.json"));
    AUREA_CHECK(fs::exists(dir / "aberto" / "shaders" / "a.frag"));
    AUREA_CHECK(!fs::exists(dir / "aberto" / ".cache"));
}

namespace {
std::string v3_manifest(const std::string& params, const std::string& extra = "", int format = 3) {
    return "{\"aureaPlugin\":" + std::to_string(format) + ",\"id\":\"com.x.v3\",\"version\":\"1.0.0\",\"name\":\"N\",\"kind\":\"shader\","
           "\"params\":[" + params + "],\"passes\":[{\"shader\":\"a.frag\"}]" + extra + "}";
}
std::string panel_manifest(const std::string& controls, int format = 3, const std::string& panelExtra = "") {
    return "{\"aureaPlugin\":" + std::to_string(format) + ",\"id\":\"com.x.ui\",\"version\":\"1.0.0\",\"name\":\"N\",\"kind\":\"panel\",\"script\":\"m.js\","
           "\"ui\":{\"panels\":[{\"id\":\"p\",\"title\":\"T\"" + panelExtra + ",\"controls\":[" + controls + "]}]}}";
}
bool refused(const std::string& json, const char* expect) {
    plugins::Manifest m;
    std::string error;
    const bool ok = plugins::parse_manifest(json, m, error);
    if (ok || error.find(expect) == std::string::npos) std::printf("\n    esperava \"%s\": %s", expect, error.c_str());
    return !ok && error.find(expect) != std::string::npos;
}
const char* kFormat3Params =
    "{\"id\":\"mix_\",\"type\":\"float\"},{\"id\":\"tone\",\"type\":\"curve\"},{\"id\":\"paint\",\"type\":\"gradient\"},"
    "{\"id\":\"top\",\"type\":\"layerImage\"},{\"id\":\"last\",\"type\":\"float\"}";
} // namespace

AUREA_TEST(PluginV3, CurveGradientAndLayerParamsTakeTheirSlots) {
    plugins::Manifest m;
    std::string error;
    AUREA_CHECK_MSG(plugins::parse_manifest(v3_manifest(kFormat3Params, ",\"uses\":[\"history\",\"audio\"],\"frameScript\":\"f.js\""), m, error), error.c_str());
    AUREA_CHECK_EQ(plugins::param_slot_at(m, 0), 0u);
    AUREA_CHECK_EQ(plugins::param_slot_at(m, 1), 1u);
    AUREA_CHECK_EQ(plugins::param_slot_at(m, 2), 17u);
    AUREA_CHECK_EQ(plugins::param_slot_at(m, 3), 49u);
    AUREA_CHECK_EQ(plugins::param_slot_at(m, 4), 50u);
    AUREA_CHECK_EQ(plugins::param_slots(m), 52u);   // + o pico do áudio
    AUREA_CHECK_EQ(plugins::extra_samplers(m), 3u);
    AUREA_CHECK(m.history && m.audio);
    AUREA_CHECK(m.frameScript == "f.js");
    const char* source =
        "vec4 aurea_main(vec2 uv) {\n"
        "    vec4 c = paint(tone(mix_));\n"
        "    if (top_connected) c += texture(top, uv);\n"
        "    return c + texture(aurea_history, uv) * aurea_audio(last) * AUREA_AUDIO_PEAK;\n"
        "}\n";
    std::vector<u32> spirv;
    AUREA_CHECK_MSG(plugins::compile_pass(m, 0, source, spirv, error), error.c_str());
    AUREA_CHECK(!spirv.empty());
    // Plugin que não usa nada disso recebe o mesmo cabeçalho de sempre.
    plugins::Manifest plain;
    AUREA_CHECK(plugins::parse_manifest(v3_manifest("{\"id\":\"a\",\"type\":\"float\"}"), plain, error));
    const std::string header = plugins::shader_header(plain, 0);
    AUREA_CHECK(header.find("aurea_curve_at") == std::string::npos);
    AUREA_CHECK(header.find("aurea_history") == std::string::npos);
}

AUREA_TEST(PluginV3, NewFeaturesAreRefusedInOlderFormatsAndPastTheLimits) {
    AUREA_CHECK(refused(v3_manifest("{\"id\":\"c\",\"type\":\"curve\"}", "", 2), "aureaPlugin"));
    AUREA_CHECK(refused(v3_manifest("", ",\"uses\":[\"history\"]", 2), "aureaPlugin"));
    AUREA_CHECK(refused(v3_manifest("", ",\"uses\":[\"depth\"]"), "history e audio"));
    AUREA_CHECK(refused(v3_manifest("", ",\"frameScript\":\"f.txt\""), "frameScript"));
    std::string many;
    for (int i = 0; i < 8; ++i) many += std::string(i ? "," : "") + "{\"id\":\"g" + std::to_string(i) + "\",\"type\":\"gradient\"}";
    AUREA_CHECK(refused(v3_manifest(many), "posicoes"));
    std::string images;
    for (int i = 0; i < 12; ++i) images += std::string(i ? "," : "") + "{\"id\":\"i" + std::to_string(i) + "\",\"type\":\"layerImage\"}";
    AUREA_CHECK(refused(v3_manifest(images), "passam de"));
}

AUREA_TEST(PluginV3, TreeInterfaceKeepsIdsAcrossTheWholeTree) {
    const std::string button = R"({"id":"b","type":"button","label":"B","onClick":"go"})";
    const std::string row = R"({"id":"r","type":"row","children":[)" + button + "]}";
    plugins::Manifest m;
    std::string error;
    AUREA_CHECK_MSG(plugins::parse_manifest(panel_manifest(R"({"id":"s","type":"section","label":"S","collapsed":true,"children":[)" + row + "]}"), m, error), error.c_str());
    AUREA_CHECK(m.ui.control("b") != nullptr);
    AUREA_CHECK(m.ui.panels[0].controls[0].collapsed);
    plugins::PluginInfo info;
    info.ui = m.ui;
    const std::string json = plugins::to_json({info});
    AUREA_CHECK(json.find("\"children\":[") != std::string::npos);
    AUREA_CHECK(json.find("\"type\":\"section\"") != std::string::npos);

    AUREA_CHECK(refused(panel_manifest(row, 2), "aureaPlugin"));
    AUREA_CHECK(refused(panel_manifest(row + "," + button), "repetido"));
    AUREA_CHECK(refused(panel_manifest(R"({"id":"t","type":"tabs","children":[)" + row + "]}"), "section"));
    AUREA_CHECK(refused(panel_manifest(R"({"id":"e","type":"row","children":[]})"), "children"));
    std::string deep = button;
    for (int i = 0; i < 7; ++i) deep = R"({"id":"n)" + std::to_string(i) + R"(","type":"column","children":[)" + deep + "]}";
    AUREA_CHECK(refused(panel_manifest(deep), "niveis"));
}

AUREA_TEST(PluginV3, PanelMayReplaceAKnownEditorPanelOnly) {
    const std::string label = R"({"id":"l","type":"label","text":"x"})";
    plugins::Manifest m;
    std::string error;
    AUREA_CHECK_MSG(plugins::parse_manifest(panel_manifest(label, 3, ",\"replaces\":\"transform\""), m, error), error.c_str());
    AUREA_CHECK(m.ui.panels[0].replaces == "transform");
    AUREA_CHECK(refused(panel_manifest(label, 3, ",\"replaces\":\"home\""), "replaces"));
    AUREA_CHECK(refused(panel_manifest(label, 2, ",\"replaces\":\"transform\""), "aureaPlugin"));
    AUREA_CHECK(refused(panel_manifest(label, 3, ",\"replaces\":\"transform\",\"placement\":\"home.tab\""), "editor.panel"));
}

AUREA_TEST(PluginV3, TwoCrashedSessionsTurnSafeModeOn) {
    const fs::path dir = fresh_dir("aurea_plugin_v3_safe");
    write_text(dir / "com.x.safe" / "plugin.json",
               R"({"aureaPlugin":1,"id":"com.x.safe","version":"1.0.0","name":"Seguro","kind":"shader","passes":[{"shader":"a.frag"}]})");
    write_text(dir / "com.x.safe" / "a.frag", "vec4 aurea_main(vec2 uv) { return texture(aurea_source, uv); }\n");
    EffectRegistry effects;
    plugins::PluginManager m(effects);
    m.set_directory(dir.string());
    const auto open = [&] { m.begin_session(); (void)m.load_all(); };
    open();
    AUREA_CHECK(!m.safe_mode());
    AUREA_CHECK(m.list().size() == 1 && m.list()[0].ok);
    open();   // a primeira caiu: ainda não é modo seguro
    AUREA_CHECK(!m.safe_mode());
    open();
    AUREA_CHECK(m.safe_mode() && m.safe_mode_automatic());
    AUREA_CHECK(m.list().size() == 1 && !m.list()[0].ok && m.list()[0].errorCode == "safe_mode");
    AUREA_CHECK(m.list()[0].id == "com.x.safe");
    // Sair zera a contagem, e uma sessão saudável não conta como queda.
    m.set_safe_mode(false);
    (void)m.load_all();
    AUREA_CHECK(!m.safe_mode() && m.list()[0].ok);
    for (int i = 0; i < 4; ++i) { open(); m.mark_healthy(); }
    AUREA_CHECK(!m.safe_mode());
    // Ligado pela pessoa, fica nas próximas aberturas.
    m.set_safe_mode(true);
    open();
    AUREA_CHECK(m.safe_mode() && !m.safe_mode_automatic());
    m.set_safe_mode(false);
    m.shutdown();
}

AUREA_TEST(PluginV3, GradientEditsAreSortedLimitedAndUndoable) {
    plugins::Manifest m;
    std::string error;
    AUREA_CHECK_MSG(plugins::parse_manifest(v3_manifest("{\"id\":\"paint\",\"type\":\"gradient\"}"), m, error), error.c_str());
    Rig r;
    AUREA_CHECK(r.e.effects().add(plugins::make_shader_effect(m, std::vector<ShaderId>(m.passes.size()), {})).ok());
    const u32 type = effect_type_id(plugins::effect_key("com.x.v3"));
    const LayerId id = r.comp()->add_layer(LayerKind::Text, "G");
    Layer* l = r.comp()->layer(id);
    EffectInstance fx;
    fx.id = l->alloc_effect_id();
    fx.type = type;
    initialize_instance(fx, *r.e.effects().params(type));
    l->effects.push_back(fx);
    const u64 layer = id.pack();
    const u32 effect = l->effects.back().id;

    f32 out[80]{};
    AUREA_CHECK_EQ(r.e.query_effect_gradient(layer, effect, 0, out, 80), 10u);
    const f32 unsorted[15] = {0.9f, 1, 0, 0, 1,  -3.0f, 0, 1, 0, 2.0f,  0.5f, 0, 0, 1, 1};
    AUREA_CHECK(r.e.set_effect_gradient(layer, effect, 0, unsorted, 15));
    AUREA_CHECK_EQ(r.e.query_effect_gradient(layer, effect, 0, out, 80), 15u);
    AUREA_CHECK_EQ(out[0], 0.f);        // ordenou e limitou a posição
    AUREA_CHECK_EQ(out[2], 1.f);        // a parada verde veio para o começo
    AUREA_CHECK_EQ(out[4], 1.f);        // alfa limitado
    AUREA_CHECK_NEAR(out[5], .5f, .0001f);
    AUREA_CHECK_NEAR(out[10], .9f, .0001f);

    const f32 one[5] = {0, 1, 1, 1, 1};
    AUREA_CHECK(!r.e.set_effect_gradient(layer, effect, 0, one, 5));
    const f32 broken[10] = {0, 1, 1, 1, 1,  1, std::numeric_limits<f32>::quiet_NaN(), 1, 1, 1};
    AUREA_CHECK(!r.e.set_effect_gradient(layer, effect, 0, broken, 10));
    AUREA_CHECK(!r.e.set_effect_gradient(layer, effect, 1, unsorted, 15));
    AUREA_CHECK_EQ(r.e.query_effect_gradient(layer, effect, 0, out, 80), 15u);

    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(undo).ok());
    AUREA_CHECK_EQ(r.e.query_effect_gradient(layer, effect, 0, out, 80), 10u);
    AUREA_CHECK_EQ(out[1], 0.f);
    AUREA_CHECK_EQ(out[6], 1.f);
}

AUREA_TEST(PluginV3, ScriptReachesExpressionsEaseTextAndMasksWithOneUndo) {
    const fs::path dir = fresh_dir("aurea_plugin_v3_api");
    write_text(dir / "com.x.api" / "plugin.json",
               R"({"aureaPlugin":3,"id":"com.x.api","version":"1.0.0","name":"Api","kind":"panel","script":"m.js"})");
    write_text(dir / "com.x.api" / "m.js", R"JS(
function montar() {
  var comp = app.project.activeItem;
  var texto = comp.layers.addText("ola");
  var doc = texto.sourceText.value;
  doc.text = "novo";
  doc.fontSize = 33;
  texto.property("Source Text").setValue(doc);
  var solida = comp.layers.addSolid([1, 0, 0], "S", 100, 100);
  solida.position.expression = "[10, 20]";
  var o = solida.opacity;
  o.setValueAtTime(0, 0);
  o.setValueAtTime(1, 100);
  o.setValueAtTime(2, 0);
  o.setTemporalEaseAtKey(2, [new KeyframeEase(0, 40)], [new KeyframeEase(-50, 50)]);
  var entrada = o.keyInTemporalEase(2)[0];
  var saida = o.keyOutTemporalEase(2)[0];
  var m = solida.addMask({vertices: [[0, 0], [10, 0], [10, 10]], closed: true});
  m.feather = 4;
  m.opacity = 50;
  m.inverted = true;
  m.mode = "subtract";
  var caminho = m.path;
  caminho.vertices[2] = [20, 30];
  m.path = caminho;
  var outra = solida.addMask({vertices: [[0, 0], [1, 0], [1, 1]]});
  outra.remove();
  var erro = "";
  try { solida.addMask({vertices: [[0]]}); } catch (e) { erro = e.message; }
  return {
    texto: texto.sourceText.value.text, corpo: texto.fontSize,
    expressao: solida.position.expression, ligada: solida.position.expressionEnabled,
    entradaInf: entrada.influence, entradaVel: entrada.speed, saidaInf: saida.influence, saidaVel: saida.speed,
    mascaras: solida.masks.length, feather: m.feather, opacidade: m.opacity, inv: m.inverted, modo: m.mode, x: m.path.vertices[2][1], erro: erro
  };
}
)JS");
    Engine e;
    EngineConfig cfg;
    cfg.workerCount = 1;
    cfg.disableAutosave = true;
    cfg.pluginDirectory = dir.string();
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    const u32 before = e.project()->timeline().composition(e.project()->timeline().current())->order().size();
    const std::string raw = e.plugin_call("com.x.api", "montar", "[]", "pt-BR");
    std::printf("\n    %s", raw.c_str());
    AUREA_CHECK_MSG(raw.find("\"ok\":true") != std::string::npos, raw.c_str());
    const auto has = [&](const char* piece) { AUREA_CHECK_MSG(raw.find(piece) != std::string::npos, piece); };
    has("\"texto\":\"novo\"");
    has("\"corpo\":33");
    has("\"expressao\":\"[10, 20]\"");
    has("\"ligada\":true");
    has("\"mascaras\":1");
    has("\"feather\":4");
    has("\"opacidade\":50");
    has("\"inv\":true");
    has("\"modo\":\"subtract\"");
    has("\"x\":30");
    has("ponto = [x, y]");
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK_EQ(comp->order().size(), before + 2);
    Layer* solid = comp->layer(comp->order().at(comp->order().size() - 1));
    const Track* opacity = solid ? solid->tracks.find(TrackProperty::Opacity) : nullptr;
    AUREA_CHECK(opacity && opacity->keys.size() == 3);
    if (opacity && opacity->keys.size() == 3) {
        AUREA_CHECK(opacity->keys[0].interp == Interpolation::Bezier);
        AUREA_CHECK_NEAR(opacity->keys[0].bx2, 0.6f, 1e-4f);      // chegando na 2ª: influência 40
        AUREA_CHECK_NEAR(opacity->keys[0].by2, 1.0f, 1e-4f);      // velocidade 0
        AUREA_CHECK_NEAR(opacity->keys[1].bx1, 0.5f, 1e-4f);      // saindo da 2ª: influência 50
        AUREA_CHECK_NEAR(opacity->keys[1].by1, 0.25f, 1e-4f);     // −50/s em 1 s sobre −100, metade da influência
    }
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    comp = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK_EQ(comp->order().size(), before);
    e.shutdown();
}
