// =============================================================================
//  AureaMCP fase C (EngineMcpTools.cpp): as ferramentas de leitura e escrita
//  precisas que os agentes chamam por Engine::assistant_call. Cada ferramenta:
//  argumento bom muda o projeto (um passo de desfazer) e a leitura confirma;
//  argumento ruim devolve {"ok":false} sem mexer em nada.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/assistant/Assistant.hpp"
#include "aurea/project/Presets.hpp"

#include <cmath>
#include <string>

using namespace aurea;

namespace {

struct McpRig {
    Engine e;
    bool ok = false;
    std::string text;   ///< id da camada de texto
    std::string solid;  ///< id do sólido
    McpRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        ok = e.initialize(ec).ok() && e.new_project(1080, 1920, 30.0, nullptr).ok();
        if (!ok) return;
        // Um texto (em cima) e um sólido, pelo mesmo sandbox do AE que o agente usa.
        const json::Value run = call("run_ae_script", R"({"code":"var c = app.project.activeItem; c.layers.addSolid([1,0,0], 'Fundo', 1080, 1920, 1); var t = c.layers.addText('Ola'); t.name = 'Titulo';"})");
        ok = okOf(run);
        const json::Value state = call("get_project_state");
        for (const auto& l : state.get("layers")->array) {
            if (l.get("kind")->string == "text") text = l.get("id")->string;
            else solid = l.get("id")->string;
        }
        ok = ok && !text.empty() && !solid.empty();
    }
    ~McpRig() { if (ok) e.shutdown(); }
    json::Value call(const char* name, const std::string& args = "{}") {
        json::Value v;
        AUREA_CHECK(json::parse(e.assistant_call(name, args), v));
        return v;
    }
    static bool okOf(const json::Value& v) { const json::Value* o = v.get("ok"); return o && o->type == json::Value::Type::Bool && o->boolean; }
    std::string L(const std::string& id) const { return "\"layer\":\"" + id + "\""; }
    json::Value details(const std::string& id) { return call("get_layer_details", "{" + L(id) + "}"); }
    static f64 num(const json::Value* v) { return v && v->is_number() ? v->number : NAN; }
    static f64 at(const json::Value* v, usize i) { return v && v->is_array() && i < v->array.size() ? v->array[i].number : NAN; }
    usize undoDepth() { bridge::EngineStatusPOD s; e.fill_status(s); return s.undoDepth; }
};

bool near(f64 a, f64 b, f64 eps = 1e-3) { return std::fabs(a - b) <= eps; }

} // namespace

AUREA_TEST(McpTools, ToolTableHasEveryPhaseCTool) {
    json::Value tools;
    AUREA_CHECK(json::parse(assistant::tools_json(), tools));
    const char* wanted[] = {"get_layer_details", "get_keyframes", "set_property", "add_keyframes", "delete_keyframes",
                            "move_keyframe", "set_keyframe_easing", "list_effect_types", "get_effect", "add_effect",
                            "remove_effect", "move_effect", "set_effect_enabled", "list_presets", "apply_preset",
                            "list_fonts", "set_text_style", "get_markers", "set_marker", "delete_marker",
                            "get_work_area", "set_work_area"};
    for (const char* name : wanted) {
        bool found = false;
        for (const auto& t : tools.array) {
            const json::Value* fn = t.get("function");
            if (fn && fn->get("name")->string == name) {
                found = true;
                AUREA_CHECK(fn->get("parameters") && fn->get("parameters")->is_object());
            }
        }
        AUREA_CHECK(found);
    }
}

AUREA_TEST(McpTools, LayerDetailsReadTransformTextAndIndex) {
    McpRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const json::Value d = r.details(r.text);
    AUREA_CHECK(McpRig::okOf(d));
    AUREA_CHECK_EQ(d.get("kind")->string, std::string("text"));
    AUREA_CHECK_EQ(d.get("name")->string, std::string("Titulo"));
    AUREA_CHECK(d.get("text") && d.get("text")->get("content")->string == "Ola");
    AUREA_CHECK(near(McpRig::num(d.get("transform")->get("opacity")->get("value")), 1.0));
    // O mesmo pelo índice do AE (1 = a de cima = o texto).
    const json::Value byIndex = r.call("get_layer_details", R"({"layer":1})");
    AUREA_CHECK(McpRig::okOf(byIndex) && byIndex.get("id")->string == r.text);
    AUREA_CHECK(!McpRig::okOf(r.call("get_layer_details", R"({"layer":"999999"})")));
    AUREA_CHECK(!McpRig::okOf(r.call("get_layer_details", R"({"layer":99})")));
}

AUREA_TEST(McpTools, SetPropertyStaticThenKeyframesAndUndo) {
    McpRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const usize depth = r.undoDepth();
    AUREA_CHECK(McpRig::okOf(r.call("set_property", "{" + r.L(r.solid) + R"(,"property":"position","value":[100,200]})")));
    json::Value d = r.details(r.solid);
    AUREA_CHECK(near(McpRig::at(d.get("transform")->get("position")->get("value"), 0), 100.0));
    AUREA_CHECK(near(McpRig::at(d.get("transform")->get("position")->get("value"), 1), 200.0));
    AUREA_CHECK(!d.get("transform")->get("position")->get("keyframes"));   // valor parado, sem keyframe
    AUREA_CHECK_EQ(r.undoDepth(), depth + 1);
    // Rotação como número = Z; escala como número = uniforme.
    AUREA_CHECK(McpRig::okOf(r.call("set_property", "{" + r.L(r.solid) + R"(,"property":"rotation","value":45})")));
    AUREA_CHECK(McpRig::okOf(r.call("set_property", "{" + r.L(r.solid) + R"(,"property":"scale","value":0.5})")));
    d = r.details(r.solid);
    AUREA_CHECK(near(McpRig::at(d.get("transform")->get("rotation")->get("value"), 2), 45.0));
    AUREA_CHECK(near(McpRig::at(d.get("transform")->get("scale")->get("value"), 1), 0.5));
    // Com seconds: keyframe. Depois, sem seconds numa propriedade animada: keyframe no cabeçote.
    const json::Value k = r.call("set_property", "{" + r.L(r.solid) + R"(,"property":"opacity","value":0,"seconds":1})");
    AUREA_CHECK(McpRig::okOf(k) && k.get("keyframe")->boolean);
    AUREA_CHECK(McpRig::okOf(r.call("set_property", "{" + r.L(r.solid) + R"(,"property":"opacity","value":7})")));   // presa em 1
    const json::Value keys = r.call("get_keyframes", "{" + r.L(r.solid) + R"(,"property":"opacity"})");
    AUREA_CHECK(McpRig::okOf(keys));
    const json::Value& ch = keys.get("channels")->array[0];
    AUREA_CHECK_EQ(ch.get("keys")->array.size(), 2u);
    AUREA_CHECK(near(ch.get("keys")->array[0].get("value")->number, 1.0));   // o do cabeçote (0 s), presa
    AUREA_CHECK(near(ch.get("keys")->array[1].get("seconds")->number, 1.0));
    // Desfazer tira um passo por chamada.
    AUREA_CHECK(McpRig::okOf(r.call("undo")));
    AUREA_CHECK_EQ(r.call("get_keyframes", "{" + r.L(r.solid) + R"(,"property":"opacity"})").get("channels")->array[0].get("keys")->array.size(), 1u);
    // Recusas: endereço, componentes demais, tipo errado.
    AUREA_CHECK(!McpRig::okOf(r.call("set_property", "{" + r.L(r.solid) + R"(,"property":"wobble","value":1})")));
    AUREA_CHECK(!McpRig::okOf(r.call("set_property", "{" + r.L(r.solid) + R"(,"property":"position","value":[1,2,3,4]})")));
    AUREA_CHECK(!McpRig::okOf(r.call("set_property", "{" + r.L(r.solid) + R"(,"property":"position","value":5})")));
    AUREA_CHECK(!McpRig::okOf(r.call("set_property", "{" + r.L(r.solid) + R"(,"property":"opacity","value":"x"})")));
}

AUREA_TEST(McpTools, KeyframesAddMoveEaseDelete) {
    McpRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const usize depth = r.undoDepth();
    const json::Value add = r.call("add_keyframes", "{" + r.L(r.solid) + R"(,"property":"position.x","keys":[{"seconds":0,"value":0,"interpolation":"ease_out"},{"seconds":1,"value":500},{"seconds":2,"value":250,"bezier":[0.2,0,0.1,1],"power":2}]})");
    AUREA_CHECK(McpRig::okOf(add));
    AUREA_CHECK_EQ(r.undoDepth(), depth + 1);   // três keys, um passo
    json::Value k = r.call("get_keyframes", "{" + r.L(r.solid) + R"(,"property":"position.x"})");
    const json::Value* keys = k.get("channels")->array[0].get("keys");
    AUREA_CHECK_EQ(keys->array.size(), 3u);
    AUREA_CHECK_EQ(keys->array[0].get("interpolation")->string, std::string("ease_out"));
    AUREA_CHECK_EQ(keys->array[2].get("interpolation")->string, std::string("bezier"));
    AUREA_CHECK(near(keys->array[2].get("bezier")->array[0].number, 0.2));
    AUREA_CHECK(near(keys->array[2].get("power")->number, 2.0));
    // Mover 1 s → 1,5 s; destino ocupado é recusado.
    AUREA_CHECK(!McpRig::okOf(r.call("move_keyframe", "{" + r.L(r.solid) + R"(,"property":"position.x","fromSeconds":1,"toSeconds":2})")));
    AUREA_CHECK(McpRig::okOf(r.call("move_keyframe", "{" + r.L(r.solid) + R"(,"property":"position.x","fromSeconds":1,"toSeconds":1.5})")));
    // Curva em todos os keys.
    AUREA_CHECK(McpRig::okOf(r.call("set_keyframe_easing", "{" + r.L(r.solid) + R"(,"property":"position.x","bezier":[0.33,0,0.67,1]})")));
    k = r.call("get_keyframes", "{" + r.L(r.solid) + R"(,"property":"position.x"})");
    keys = k.get("channels")->array[0].get("keys");
    AUREA_CHECK(near(keys->array[1].get("seconds")->number, 1.5));
    for (const auto& key : keys->array) AUREA_CHECK_EQ(key.get("interpolation")->string, std::string("bezier"));
    AUREA_CHECK(!McpRig::okOf(r.call("set_keyframe_easing", "{" + r.L(r.solid) + R"(,"property":"position.x","interpolation":"sideways"})")));
    // Apagar um, depois todos.
    AUREA_CHECK(McpRig::okOf(r.call("delete_keyframes", "{" + r.L(r.solid) + R"(,"property":"position.x","seconds":[1.5]})")));
    AUREA_CHECK_EQ(r.call("get_keyframes", "{" + r.L(r.solid) + R"(,"property":"position.x"})").get("channels")->array[0].get("keys")->array.size(), 2u);
    AUREA_CHECK(McpRig::okOf(r.call("delete_keyframes", "{" + r.L(r.solid) + R"(,"property":"position.x","all":true})")));
    AUREA_CHECK(!McpRig::okOf(r.call("delete_keyframes", "{" + r.L(r.solid) + R"(,"property":"position.x","all":true})")));
    // Sem propriedade: lista os canais animados (nenhum agora).
    const json::Value all = r.call("get_keyframes", "{" + r.L(r.solid) + "}");
    AUREA_CHECK(McpRig::okOf(all) && all.get("channels")->array.empty());
}

AUREA_TEST(McpTools, EffectsAddReadSetReorderToggleRemove) {
    McpRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const json::Value types = r.call("list_effect_types", R"({"query":"gaussian"})");
    AUREA_CHECK(McpRig::okOf(types) && !types.get("effects")->array.empty());
    // Pela matchName do AE (Desfoque gaussiano) e pela chave do Aurea.
    const json::Value blur = r.call("add_effect", "{" + r.L(r.solid) + R"(,"type":"ADBE Gaussian Blur 2"})");
    AUREA_CHECK(McpRig::okOf(blur));
    const std::string blurId = std::to_string(static_cast<i64>(blur.get("effectId")->number));
    const std::string key = types.get("effects")->array[0].get("key")->string;
    const json::Value second = r.call("add_effect", "{" + r.L(r.solid) + R"(,"type":")" + key + R"("})");
    AUREA_CHECK(McpRig::okOf(second));
    AUREA_CHECK(!McpRig::okOf(r.call("add_effect", "{" + r.L(r.solid) + R"(,"type":"no such effect at all"})")));
    json::Value d = r.details(r.solid);
    AUREA_CHECK_EQ(d.get("effects")->array.size(), 2u);
    // Parâmetros: o primeiro float animável vira alvo de set_property.
    const json::Value fx = r.call("get_effect", "{" + r.L(r.solid) + ",\"effect\":" + blurId + "}");
    AUREA_CHECK(McpRig::okOf(fx));
    std::string param;
    for (const auto& p : fx.get("params")->array)
        if (param.empty() && p.get("type")->string == "float" && !p.get("animatable")) param = std::to_string(static_cast<i64>(p.get("index")->number));
    AUREA_CHECK(!param.empty());
    if (!param.empty()) {
        AUREA_CHECK(McpRig::okOf(r.call("set_property", "{" + r.L(r.solid) + R"(,"property":"effect.)" + blurId + "." + param + R"(","value":12})")));
        const json::Value after = r.call("get_effect", "{" + r.L(r.solid) + ",\"effect\":" + blurId + "}");
        for (const auto& p : after.get("params")->array)
            if (std::to_string(static_cast<i64>(p.get("index")->number)) == param) AUREA_CHECK(near(p.get("value")->number, 12.0));
        // Animado: dois keyframes no parâmetro.
        AUREA_CHECK(McpRig::okOf(r.call("add_keyframes", "{" + r.L(r.solid) + R"(,"property":"effect.)" + blurId + "." + param + R"(","keys":[{"seconds":0,"value":0},{"seconds":1,"value":30}]})")));
        AUREA_CHECK_EQ(r.call("get_keyframes", "{" + r.L(r.solid) + R"(,"property":"effect.)" + blurId + "." + param + "\"}").get("channels")->array[0].get("keys")->array.size(), 2u);
    }
    // Ordem, liga/desliga, tirar.
    AUREA_CHECK(McpRig::okOf(r.call("move_effect", "{" + r.L(r.solid) + ",\"effect\":" + blurId + ",\"index\":1}")));
    d = r.details(r.solid);
    AUREA_CHECK_EQ(std::to_string(static_cast<i64>(d.get("effects")->array[1].get("id")->number)), blurId);
    AUREA_CHECK(McpRig::okOf(r.call("set_effect_enabled", "{" + r.L(r.solid) + ",\"effect\":" + blurId + ",\"enabled\":false}")));
    AUREA_CHECK(!r.details(r.solid).get("effects")->array[1].get("enabled")->boolean);
    AUREA_CHECK(McpRig::okOf(r.call("remove_effect", "{" + r.L(r.solid) + ",\"effect\":" + blurId + "}")));
    AUREA_CHECK_EQ(r.details(r.solid).get("effects")->array.size(), 1u);
    AUREA_CHECK(!McpRig::okOf(r.call("remove_effect", "{" + r.L(r.solid) + ",\"effect\":" + blurId + "}")));
}

AUREA_TEST(McpTools, TextStyleFontsAndPresets) {
    McpRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const usize depth = r.undoDepth();
    AUREA_CHECK(McpRig::okOf(r.call("set_text_style", "{" + r.L(r.text) + R"(,"text":"Novo titulo","size":120,"color":[1,0.5,0],"alignment":"center","tracking":50,"strokeWidth":4})")));
    AUREA_CHECK_EQ(r.undoDepth(), depth + 1);   // tudo num passo
    const json::Value t = *r.details(r.text).get("text");
    AUREA_CHECK_EQ(t.get("content")->string, std::string("Novo titulo"));
    AUREA_CHECK(near(t.get("size")->number, 120.0));
    AUREA_CHECK(near(t.get("color")->array[1].number, 0.5));
    AUREA_CHECK_EQ(t.get("alignment")->string, std::string("center"));
    AUREA_CHECK(near(t.get("tracking")->number, 50.0));
    AUREA_CHECK(near(t.get("strokeWidth")->number, 4.0));
    AUREA_CHECK(!McpRig::okOf(r.call("set_text_style", "{" + r.L(r.solid) + R"(,"size":20})")));   // não é texto
    AUREA_CHECK(!McpRig::okOf(r.call("set_text_style", "{" + r.L(r.text) + R"(,"font":"Fonte Que Nao Existe 123"})")));
    AUREA_CHECK(!McpRig::okOf(r.call("set_text_style", "{" + r.L(r.text) + R"(,"alignment":"diagonal"})")));
    // Fontes: a lista vem do aparelho (pode estar vazia no host); a chamada responde.
    AUREA_CHECK(McpRig::okOf(r.call("list_fonts", R"({"limit":5})")));
    // text.tracking por set_property (valor parado).
    AUREA_CHECK(McpRig::okOf(r.call("set_property", "{" + r.L(r.text) + R"(,"property":"text.tracking","value":-20})")));
    AUREA_CHECK(near(r.details(r.text).get("text")->get("tracking")->number, -20.0));
    // Presets de texto: lista com os dois modelos; aplicar pelo nome e pelo id.
    const json::Value list = r.call("list_presets", R"({"kind":"text"})");
    AUREA_CHECK(McpRig::okOf(list) && list.get("presets")->array.size() > 20);
    AUREA_CHECK(McpRig::okOf(r.call("apply_preset", "{" + r.L(r.text) + R"(,"kind":"text","preset":"Typewriter"})")));
    AUREA_CHECK(McpRig::okOf(r.call("apply_preset", "{" + r.L(r.text) + R"(,"kind":"text","preset":1000})")));
    AUREA_CHECK(!McpRig::okOf(r.call("apply_preset", "{" + r.L(r.solid) + R"(,"kind":"text","preset":0})")));
    AUREA_CHECK(!McpRig::okOf(r.call("apply_preset", "{" + r.L(r.text) + R"(,"kind":"text","preset":"nope"})")));
    AUREA_CHECK(!McpRig::okOf(r.call("apply_preset", "{" + r.L(r.text) + R"(,"kind":"json","json":"{not json"})")));
    AUREA_CHECK(!McpRig::okOf(r.call("list_presets", R"({"kind":"effect"})")));
}

AUREA_TEST(McpTools, MarkersAndWorkArea) {
    McpRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    AUREA_CHECK(McpRig::okOf(r.call("set_marker", R"({"seconds":1,"label":"Batida","comment":"entra o logo","color":"#FF0000","durationSeconds":0.5})")));
    json::Value m = r.call("get_markers");
    AUREA_CHECK(McpRig::okOf(m) && m.get("composition")->array.size() == 1u);
    const json::Value& first = m.get("composition")->array[0];
    AUREA_CHECK_EQ(first.get("label")->string, std::string("Batida"));
    AUREA_CHECK_EQ(first.get("color")->string, std::string("#FF0000"));
    AUREA_CHECK(near(first.get("durationSeconds")->number, 0.5));
    // Atualizar mantém o que não foi passado.
    const json::Value upd = r.call("set_marker", R"({"seconds":1,"label":"Logo"})");
    AUREA_CHECK(McpRig::okOf(upd) && !upd.get("created")->boolean);
    m = r.call("get_markers");
    AUREA_CHECK_EQ(m.get("composition")->array[0].get("comment")->string, std::string("entra o logo"));
    AUREA_CHECK(!McpRig::okOf(r.call("set_marker", R"({"seconds":1,"color":"red"})")));
    // Marca da camada.
    AUREA_CHECK(McpRig::okOf(r.call("set_marker", "{" + r.L(r.text) + R"(,"seconds":0.5,"label":"fala"})")));
    m = r.call("get_markers", "{" + r.L(r.text) + "}");
    AUREA_CHECK(m.get("layer") && m.get("layer")->array.size() == 1u);
    AUREA_CHECK(McpRig::okOf(r.call("delete_marker", "{" + r.L(r.text) + R"(,"seconds":0.5})")));
    AUREA_CHECK(McpRig::okOf(r.call("delete_marker", R"({"seconds":1})")));
    AUREA_CHECK(!McpRig::okOf(r.call("delete_marker", R"({"seconds":1})")));
    // Área de trabalho.
    AUREA_CHECK(McpRig::okOf(r.call("set_work_area", R"({"startSeconds":1,"endSeconds":2})")));
    json::Value wa = r.call("get_work_area");
    AUREA_CHECK(wa.get("active")->boolean && near(wa.get("startSeconds")->number, 1.0) && near(wa.get("endSeconds")->number, 2.0));
    AUREA_CHECK(!McpRig::okOf(r.call("set_work_area", R"({"startSeconds":3,"endSeconds":2})")));
    AUREA_CHECK(McpRig::okOf(r.call("set_work_area", R"({"clear":true})")));
    AUREA_CHECK(!r.call("get_work_area").get("active")->boolean);
}

AUREA_TEST(McpTools, LockedLayerAndBadArgumentsChangeNothing) {
    McpRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const usize depth = r.undoDepth();
    AUREA_CHECK(!McpRig::okOf(r.call("set_property", R"({"property":"opacity","value":1})")));
    AUREA_CHECK(!McpRig::okOf(r.call("add_keyframes", "{" + r.L(r.solid) + R"(,"property":"opacity","keys":[]})")));
    AUREA_CHECK(!McpRig::okOf(r.call("add_keyframes", "{" + r.L(r.solid) + R"(,"property":"opacity","keys":[{"seconds":-1,"value":1}]})")));
    AUREA_CHECK(!McpRig::okOf(r.call("move_keyframe", "{" + r.L(r.solid) + R"(,"property":"opacity","fromSeconds":0,"toSeconds":1})")));
    AUREA_CHECK(!McpRig::okOf(r.call("get_effect", "{" + r.L(r.solid) + R"(,"effect":77})")));
    AUREA_CHECK_EQ(r.undoDepth(), depth);
    // Camada travada: o agente lê, mas não escreve.
    AUREA_CHECK(McpRig::okOf(r.call("run_ae_script", R"({"code":"app.project.activeItem.layer(2).locked = true;"})")));
    if (r.details(r.solid).get("locked")->boolean)
        AUREA_CHECK(!McpRig::okOf(r.call("set_property", "{" + r.L(r.solid) + R"(,"property":"opacity","value":0.5})")));
}
