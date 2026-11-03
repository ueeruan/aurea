// =============================================================================
//  Testes do ASSISTENTE de IA (assistant/Assistant.hpp + Engine::assistant_call).
//
//  O contrato: as ferramentas são JSON válido no formato da OpenAI; cada
//  chamada devolve JSON com "ok"; o script do AE cria de verdade e é UM passo
//  de desfazer; argumento ruim nunca derruba nem muda o projeto.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/assistant/Assistant.hpp"
#include "aurea/project/Presets.hpp"

#include <string>

using namespace aurea;

namespace {

struct AssistantRig {
    Engine e;
    bool ok = false;
    AssistantRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        ok = e.initialize(ec).ok() && e.new_project(1080, 1920, 30.0, nullptr).ok();
    }
    ~AssistantRig() { if (ok) e.shutdown(); }
    json::Value call(const char* name, const std::string& args = "{}") {
        json::Value v;
        AUREA_CHECK(json::parse(e.assistant_call(name, args), v));
        return v;
    }
    static bool okOf(const json::Value& v) { const json::Value* o = v.get("ok"); return o && o->type == json::Value::Type::Bool && o->boolean; }
    usize layers() {
        const json::Value s = call("get_project_state");
        const json::Value* l = s.get("layers");
        return l && l->is_array() ? l->array.size() : 0;
    }
};

} // namespace

AUREA_TEST(Assistant, ToolsAreValidOpenAiFunctions) {
    json::Value tools;
    AUREA_CHECK(json::parse(assistant::tools_json(), tools));
    AUREA_CHECK(tools.is_array() && tools.array.size() >= 10);
    for (const auto& t : tools.array) {
        const json::Value* fn = t.get("function");
        AUREA_CHECK(fn && fn->get("name") && fn->get("name")->is_string());
        AUREA_CHECK(fn && fn->get("parameters") && fn->get("parameters")->is_object());
    }
    AUREA_CHECK(assistant::system_prompt("pt-BR", "0.4.6").find("pt-BR") != std::string::npos);
}

AUREA_TEST(Assistant, ScriptCreatesLayersAsOneUndoStep) {
    AssistantRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const json::Value state = r.call("get_project_state");
    AUREA_CHECK(AssistantRig::okOf(state));
    AUREA_CHECK_EQ(state.get("composition")->get("width")->number, 1080.0);
    const usize before = r.layers();
    const json::Value run = r.call("run_ae_script", R"({"code":"var c = app.project.activeItem; var t = c.layers.addText('Olá'); t.name = 'Titulo'; t.property('ADBE Transform Group').property('ADBE Opacity').setValueAtTime(0, 0); t.property('ADBE Transform Group').property('ADBE Opacity').setValueAtTime(1, 100); c.layers.addSolid([1,0,0], 'Fundo', 1080, 1920, 1);"})");
    AUREA_CHECK(AssistantRig::okOf(run));
    AUREA_CHECK_EQ(r.layers(), before + 2);
    AUREA_CHECK(AssistantRig::okOf(r.call("undo")));
    AUREA_CHECK_EQ(r.layers(), before);   // as duas camadas saem num passo só
    AUREA_CHECK(AssistantRig::okOf(r.call("redo")));
    AUREA_CHECK_EQ(r.layers(), before + 2);
}

AUREA_TEST(Assistant, SelectDeleteViewAndPanels) {
    AssistantRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    (void)r.call("run_ae_script", R"({"code":"app.project.activeItem.layers.addNull();"})");
    const json::Value state = r.call("get_project_state");
    const json::Value* layers = state.get("layers");
    AUREA_CHECK(layers && !layers->array.empty()); if (!layers || layers->array.empty()) return;
    const std::string id = layers->array[0].get("id")->string;
    AUREA_CHECK(AssistantRig::okOf(r.call("select_layers", "{\"ids\":[\"" + id + "\"]}")));
    AUREA_CHECK_EQ(r.call("get_project_state").get("selection")->array.size(), 1u);
    AUREA_CHECK(AssistantRig::okOf(r.call("delete_layers", "{\"ids\":[\"" + id + "\"]}")));
    AUREA_CHECK_EQ(r.layers(), layers->array.size() - 1);
    AUREA_CHECK(AssistantRig::okOf(r.call("set_view", R"({"mode":"top"})")));
    AUREA_CHECK_EQ(r.e.view_mode(), 6u);
    const json::Value panel = r.call("open_panel", R"({"panel":"export"})");
    AUREA_CHECK(AssistantRig::okOf(panel) && panel.get("ui") && panel.get("ui")->string == "open_panel");
    AUREA_CHECK(AssistantRig::okOf(r.call("seek", R"({"seconds":1.5})")));
}

AUREA_TEST(Assistant, EffectsListComesFromTheAeTable) {
    AssistantRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const json::Value v = r.call("list_ae_effects", R"({"query":"blur"})");
    AUREA_CHECK(AssistantRig::okOf(v));
    bool gaussian = false;
    for (const auto& fx : v.get("effects")->array) gaussian |= fx.get("matchName")->string == "ADBE Gaussian Blur 2";
    AUREA_CHECK(gaussian);
}

AUREA_TEST(Assistant, BadCallsReportErrorsAndChangeNothing) {
    AssistantRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const usize before = r.layers();
    AUREA_CHECK(!AssistantRig::okOf(r.call("format_disk")));
    AUREA_CHECK(!AssistantRig::okOf(r.call("run_ae_script", "{}")));
    AUREA_CHECK(!AssistantRig::okOf(r.call("delete_layers", R"({"ids":[1.5]})")));
    AUREA_CHECK(!AssistantRig::okOf(r.call("set_view", R"({"mode":"sideways"})")));
    AUREA_CHECK(!AssistantRig::okOf(r.call("seek", R"({"seconds":-3})")));
    json::Value v;
    AUREA_CHECK(json::parse(r.e.assistant_call("get_project_state", "not json"), v) && !AssistantRig::okOf(v));
    // O sandbox bloqueia arquivo: relata, não executa.
    (void)r.call("run_ae_script", R"({"code":"var f = new File('/etc/passwd'); f.open('r');"})");
    AUREA_CHECK_EQ(r.layers(), before);
}
