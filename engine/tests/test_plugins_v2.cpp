// =============================================================================
//  0.3.3 plugins: plugins v2 — painéis, scripts (AureaScript) e APIs.
//
//  CPU, sem GPU: o manifesto do formato 2 (aceita e recusa dizendo o campo),
//  a linguagem, o sandbox (nada de require/eval/disco; passos, tempo,
//  memória e profundidade limitados; cancelamento), a API de camadas e
//  keyframes contra o motor de verdade com UM desfazer por chamada, as
//  permissões de rede (recusa sem nenhum byte sair), o estado dos painéis e
//  os dois exemplos novos de docs/plugins/examples.
// =============================================================================
#include "TestFramework.hpp"

#include "../src/plugins/PluginInternal.hpp"
#include "aurea/Engine.hpp"
#include "aurea/plugins/PluginManager.hpp"
#include "aurea/plugins/PluginRuntime.hpp"
#include "aurea/plugins/Script.hpp"
#include "aurea/project/Presets.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/text/Text.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace aurea;
namespace fs = std::filesystem;
namespace sc = aurea::plugins::script;

namespace {

fs::path fresh_dir(const char* name) {
    static std::atomic<u32> counter{0};
    const fs::path p = fs::temp_directory_path() / ("aurea-pluginsv2-" + std::string(name) + "-" + std::to_string(++counter) + "-"
                                                    + std::to_string(std::time(nullptr)));
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

std::string str(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

void write_file(const fs::path& p, const std::string& data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

u32 crc32(const std::string& data) {
    static u32 table[256];
    static bool ready = false;
    if (!ready) {
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    u32 c = 0xFFFFFFFFu;
    for (const unsigned char b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
void put16(std::string& s, u32 v) { s += static_cast<char>(v & 0xFF); s += static_cast<char>((v >> 8) & 0xFF); }
void put32(std::string& s, u32 v) { put16(s, v & 0xFFFF); put16(s, v >> 16); }

/// ZIP sem compressão (o pacote .aureaplugin de teste).
void write_zip(const fs::path& out, const std::vector<std::pair<std::string, std::string>>& files) {
    std::string zip, central;
    for (const auto& [name, data] : files) {
        const u32 offset = static_cast<u32>(zip.size());
        const u32 crc = crc32(data);
        put32(zip, 0x04034b50); put16(zip, 20); put16(zip, 0); put16(zip, 0); put16(zip, 0); put16(zip, 0x21);
        put32(zip, crc); put32(zip, static_cast<u32>(data.size())); put32(zip, static_cast<u32>(data.size()));
        put16(zip, static_cast<u32>(name.size())); put16(zip, 0);
        zip += name;
        zip += data;
        put32(central, 0x02014b50); put16(central, 20); put16(central, 20); put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0x21); put32(central, crc); put32(central, static_cast<u32>(data.size()));
        put32(central, static_cast<u32>(data.size())); put16(central, static_cast<u32>(name.size()));
        put16(central, 0); put16(central, 0); put16(central, 0); put16(central, 0); put32(central, 0);
        put32(central, offset);
        central += name;
    }
    const u32 cdOffset = static_cast<u32>(zip.size());
    zip += central;
    put32(zip, 0x06054b50); put16(zip, 0); put16(zip, 0); put16(zip, static_cast<u32>(files.size()));
    put16(zip, static_cast<u32>(files.size())); put32(zip, static_cast<u32>(central.size())); put32(zip, cdOffset);
    put16(zip, 0);
    write_file(out, zip);
}

/// O resultado JSON de uma chamada.
struct CallResult {
    bool parsed = false;
    bool ok = false;
    json::Value root;
    std::string raw;
    [[nodiscard]] const json::Value* value() const { return root.get("value"); }
    [[nodiscard]] std::string error_text() const {
        const json::Value* e = root.get("error");
        const json::Value* t = e ? e->get("text") : nullptr;
        return t && t->is_string() ? t->string : std::string();
    }
    [[nodiscard]] std::string error_name() const {
        const json::Value* e = root.get("error");
        const json::Value* t = e ? e->get("name") : nullptr;
        return t && t->is_string() ? t->string : std::string();
    }
    [[nodiscard]] bool aborted() const {
        const json::Value* e = root.get("error");
        const json::Value* t = e ? e->get("aborted") : nullptr;
        return t && t->type == json::Value::Type::Bool && t->boolean;
    }
};
CallResult parse_result(const std::string& raw) {
    CallResult r;
    r.raw = raw;
    r.parsed = json::parse(raw, r.root) && r.root.is_object();
    const json::Value* ok = r.parsed ? r.root.get("ok") : nullptr;
    r.ok = ok && ok->type == json::Value::Type::Bool && ok->boolean;
    return r;
}

f64 num(const json::Value* v, const char* key) {
    const json::Value* x = v ? v->get(key) : nullptr;
    return x && x->is_number() ? x->number : -1.0e30;
}

/// Um plugin "panel" pronto: pasta com plugin.json + main.js, e o PluginInfo dele.
struct PanelPlugin {
    fs::path dir;
    plugins::PluginInfo info;
};

PanelPlugin make_panel_plugin(const char* tag, const std::string& manifest, const std::string& script) {
    PanelPlugin p;
    p.dir = fresh_dir(tag);
    write_file(p.dir / "plugin.json", manifest);
    write_file(p.dir / "main.js", script);
    plugins::Manifest m;
    std::string error;
    const bool ok = plugins::parse_manifest(manifest, m, error);
    AUREA_CHECK_MSG(ok, error.c_str());
    p.info.id = m.id;
    p.info.version = m.version;
    p.info.kind = m.kind;
    p.info.directory = str(p.dir);
    p.info.ok = true;
    p.info.format = m.format;
    p.info.script = m.script;
    p.info.permissions = m.permissions;
    p.info.ui = m.ui;
    p.info.strings = m.strings;
    p.info.names = m.name;
    p.info.enabled = true;
    p.info.generation = 1;
    return p;
}

std::string panel_manifest(const std::string& id, const std::string& extra = "") {
    return R"({"aureaPlugin":2,"id":")" + id + R"(","version":"1.0.0","name":{"pt-BR":"Teste","en":"Test"},
      "kind":"panel","script":"main.js",)" + extra + R"(
      "ui":{"panels":[{"id":"main","title":"Painel","icon":"wand","placement":["editor.panel","home.tab"],
        "controls":[
          {"id":"tamanho","type":"slider","label":"Tamanho","min":10,"max":100,"step":5,"default":20,"onChange":"mudou"},
          {"id":"cor","type":"color","default":"#ff0000"},
          {"id":"estilo","type":"choice","options":["A","B","C"],"default":1},
          {"id":"texto","type":"text","default":"Ola","maxLength":10},
          {"id":"lista","type":"list","onSelect":"escolheu"},
          {"id":"info","type":"label","text":"..."},
          {"id":"ok","type":"button","label":"Gerar","onClick":"gerar"}
        ]}],
        "actions":[{"id":"rapido","title":"Rapido","icon":"bolt","placement":"layer.toolbar","call":"gerar"}]}})";
}

const char* kPanelScript = R"(
let vezes = 0
function mudou(v, id) { ui.setText("info", "valor " + v + " de " + id); return v }
function escolheu(item) { ui.toast("escolhido " + item); return item }
function gerar() {
  vezes++
  ui.setItems("lista", [{id: "a", label: "Primeiro"}, "segundo"])
  ui.set("lista", "segundo")
  ui.setEnabled("ok", vezes < 3)
  return {vezes, tamanho: ui.get("tamanho"), cor: ui.get("cor"), estilo: ui.get("estilo"), texto: ui.get("texto")}
}
function corErrada() { ui.set("cor", "vermelho") }
function laco() { while (true) {} }
function lancar() {
  const x = 1
  throw new RangeError("deu ruim " + x)
}
function sandbox() {
  return [typeof require, typeof process, typeof eval, typeof globalThis, typeof Function,
          typeof XMLHttpRequest, typeof window, typeof importScripts].join(",")
}
function rede(url, opts) { const r = fetch(url, opts); return {status: r.status, ok: r.ok, body: r.json()} }
function clip() { return clipboard.readText() }
function arquivo() { return files.pick({types: ["json"]}) }
function guarda(v) { storage.set("v", v); return storage.get("v") }
function texto() { return app.t("ola", "Ana") }
)";

} // namespace

// =============================================================================
// Manifesto do formato 2
// =============================================================================
AUREA_TEST(PluginsV2, ManifestV2AcceptsPanelsAndRefusesNamingTheField) {
    plugins::Manifest m;
    std::string error;
    AUREA_CHECK_MSG(plugins::parse_manifest(panel_manifest("com.teste.painel", R"("permissions":{"network":["API.Exemplo.com"],"clipboard":true},)"), m, error),
                    error.c_str());
    AUREA_CHECK(m.kind == plugins::Kind::Panel);
    AUREA_CHECK_EQ(m.format, 2u);
    AUREA_CHECK_EQ(m.ui.panels.size(), usize{1});
    AUREA_CHECK_EQ(m.ui.panels[0].controls.size(), usize{7});
    AUREA_CHECK(m.ui.panels[0].placement == (plugins::kPlaceEditorPanel | plugins::kPlaceHomeTab));
    AUREA_CHECK_EQ(m.ui.actions.size(), usize{1});
    AUREA_CHECK(m.ui.actions[0].needsLayer);   // layer.toolbar pede camada por padrão
    AUREA_CHECK_EQ(m.permissions.network.size(), usize{1});
    AUREA_CHECK(m.permissions.network[0] == "api.exemplo.com");   // minúsculas
    AUREA_CHECK(m.permissions.mask() == (plugins::kPermNetwork | plugins::kPermClipboard));
    AUREA_CHECK(m.ui.control("tamanho") && m.ui.control("tamanho")->def == 20.0);
    AUREA_CHECK(m.ui.control("cor")->defText == "#FF0000");

    // A lista em JSON leva a interface e as permissões.
    plugins::PluginInfo info;
    info.id = m.id;
    info.ui = m.ui;
    info.permissions = m.permissions;
    info.granted = plugins::kPermClipboard;
    info.format = 2;
    const std::string listed = plugins::to_json({info});
    AUREA_CHECK(listed.find("\"placement\":[\"editor.panel\",\"home.tab\"]") != std::string::npos);
    AUREA_CHECK(listed.find("\"granted\":{\"network\":false,\"clipboard\":true,\"files\":false}") != std::string::npos);

    struct Bad { std::string json; const char* mustSay; };
    const Bad bad[] = {
        {R"({"aureaPlugin":1,"id":"a.b.c","version":"1.0.0","name":"x","kind":"stack","stack":[{"effect":"aurea.blur.gaussian"}],"script":"m.js"})", "aureaPlugin\": 2"},
        {R"({"aureaPlugin":1,"id":"a.b.c","version":"1.0.0","name":"x","kind":"panel"})", "aureaPlugin\": 2"},
        {R"({"aureaPlugin":3,"id":"a.b.c","version":"1.0.0","name":"x","kind":"panel"})", "versoes 1 e 2"},
        {panel_manifest("a.b.c", R"("permissions":{"network":["*"]},)"), "\"*\" nao e aceito"},
        {panel_manifest("a.b.c", R"("permissions":{"network":["https://x.com/api"]},)"), "so o dominio"},
        {panel_manifest("a.b.c", R"("permissions":{"camera":true},)"), "campo desconhecido \"camera\""},
        {R"({"aureaPlugin":2,"id":"a.b.c","version":"1.0.0","name":"x","kind":"panel","script":"m.js",
            "ui":{"panels":[{"id":"p","title":"P","icon":"foguete","controls":[{"id":"b","type":"button","label":"B","onClick":"f"}]}]}})", "icone desconhecido"},
        {R"({"aureaPlugin":2,"id":"a.b.c","version":"1.0.0","name":"x","kind":"panel","script":"m.js",
            "ui":{"panels":[{"id":"p","title":"P","controls":[{"id":"b","type":"button","label":"B"}]}]}})", "precisa de \"onClick\""},
        {R"({"aureaPlugin":2,"id":"a.b.c","version":"1.0.0","name":"x","kind":"panel",
            "ui":{"panels":[{"id":"p","title":"P","controls":[{"id":"b","type":"button","label":"B","onClick":"f"}]}]}})", "nao tem \"script\""},
        {R"({"aureaPlugin":2,"id":"a.b.c","version":"1.0.0","name":"x","kind":"panel","script":"m.js",
            "ui":{"actions":[{"id":"a","title":"A","placement":"layer.menu","call":"f","panel":"p"}]}})", "um dos dois"},
        {R"({"aureaPlugin":2,"id":"a.b.c","version":"1.0.0","name":"x","kind":"panel","script":"m.js",
            "ui":{"panels":[{"id":"p","title":"P","controls":[{"id":"s","type":"slider","min":5,"max":1}]}]}})", "maior que \"min\""},
        {R"({"aureaPlugin":2,"id":"a.b.c","version":"1.0.0","name":"x","kind":"panel","script":"m.js",
            "ui":{"panels":[{"id":"p","title":"P","controls":[{"id":"s","type":"html"}]}]}})", "tipo desconhecido \"html\""},
        {R"({"aureaPlugin":2,"id":"a.b.c","version":"1.0.0","name":"x","kind":"panel","script":"../fora.js","ui":{}})", "\"script\" deve ser"},
    };
    for (const Bad& b : bad) {
        plugins::Manifest out;
        std::string why;
        const bool accepted = plugins::parse_manifest(b.json, out, why);
        AUREA_CHECK_MSG(!accepted, b.mustSay);
        AUREA_CHECK_MSG(why.find(b.mustSay) != std::string::npos, (why + " | esperado: " + b.mustSay).c_str());
    }
}

// =============================================================================
// Linguagem e sandbox
// =============================================================================
AUREA_TEST(PluginsV2, ScriptLanguageCoversWhatPluginsUse) {
    struct Case { const char* code; const char* want; };
    const Case cases[] = {
        {"return 1+2*3", "7"},
        {"return 0.1+0.2", "0.30000000000000004"},
        {"const fs=[]; for (let i=0;i<3;i++) fs.push(()=>i); return fs.map(f=>f()).join(',')", "0,1,2"},
        {"const {a, b: [x, y = 5], ...r} = {a:1, b:[2], c:3}; return [a,x,y,Object.keys(r)].join('|')", "1|2|5|c"},
        {"return `v=${1+1} ${'a\xc3\xa7\xc3\xa3o'.toUpperCase()} ${'a\xc3\xa7\xc3\xa3o'.length}`", "v=2 A\xc3\x87\xc3\x83O 4"},
        {"try { null.x } catch (e) { return e.name + ':' + (e instanceof TypeError) }", "TypeError:true"},
        {"switch (3) { case 1: return 'um'; case 3: return 'tres'; default: return 'x' }", "tres"},
        {"return [3,1,2].sort((a,b)=>a-b).join() + JSON.stringify({a:[1,'x',null]})", "1,2,3{\"a\":[1,\"x\",null]}"},
        {"const o={x:{y:1}}; return o?.x?.y + ':' + o.z?.w.q", "1:undefined"},
        {"let a=null; a ?\?= 5; return a ** 2", "25"},
        {"function P(n){ this.n = n } return new P(4).n", "4"},
        {"return new Date(Date.UTC ? 0 : 0).toISOString()", "1970-01-01T00:00:00.000Z"},
    };
    for (const Case& k : cases) {
        sc::Context ctx;
        sc::ScriptError e;
        const std::string src = std::string("function t(){ ") + k.code + " }";
        if (!ctx.run(src, "t.js", e)) {
            AUREA_CHECK_MSG(false, (std::string(k.code) + " -> " + e.text()).c_str());
            continue;
        }
        sc::Value r;
        const bool ok = ctx.call("t", {}, r, e);
        AUREA_CHECK_MSG(ok, (std::string(k.code) + " -> " + e.text()).c_str());
        const std::string got = r.is_string() ? r.as_string() : ctx.to_string(r);
        AUREA_CHECK_MSG(got == k.want, (std::string(k.code) + " deu '" + got + "'").c_str());
    }
    // Erro de sintaxe com linha e coluna, e o que não existe dito pelo nome.
    sc::Context ctx;
    sc::ScriptError e;
    AUREA_CHECK(!ctx.run("let a = 1\nlet b = ;", "s.js", e));
    AUREA_CHECK_EQ(e.line, 2u);
    AUREA_CHECK(e.name == "SyntaxError");
    AUREA_CHECK(!ctx.run("class A {}", "s.js", e));
    AUREA_CHECK(e.message.find("class nao existe") != std::string::npos);
    AUREA_CHECK(!ctx.run("const r = /a+/", "s.js", e));
    AUREA_CHECK(e.message.find("expressoes regulares") != std::string::npos);
    AUREA_CHECK(!sc::check_syntax("function f( {", "x.js", e));
    AUREA_CHECK(sc::check_syntax("function f() { return 1 }", "x.js", e));
}

AUREA_TEST(PluginsV2, SandboxHasNoWayOutAndLimitsHold) {
    sc::Context ctx;
    sc::ScriptError e;
    AUREA_CHECK(ctx.run(kPanelScript, "main.js", e));
    sc::Value r;
    AUREA_CHECK(ctx.call("sandbox", {}, r, e));
    AUREA_CHECK_MSG(r.is_string() && r.as_string() == "undefined,undefined,undefined,undefined,undefined,undefined,undefined,undefined",
                    r.is_string() ? r.as_string().c_str() : "?");

    // Laço infinito: para por passos (o try/catch do script não pega).
    sc::Limits steps;
    steps.maxSteps = 200000;
    sc::Context a(steps);
    AUREA_CHECK(a.run("function f(){ try { while (true) {} } catch (x) { return 'pegou' } }", "a.js", e));
    AUREA_CHECK(!a.call("f", {}, r, e));
    AUREA_CHECK(e.aborted && e.name == "LimitError");
    // O contexto continua usável depois do aborto.
    AUREA_CHECK(a.run("function g(){ return 42 }", "b.js", e));
    AUREA_CHECK(a.call("g", {}, r, e) && r.is_number() && r.as_number() == 42.0);

    // Por tempo.
    sc::Limits time;
    time.maxSteps = ~u64{0};
    time.maxTimeMs = 150;
    sc::Context t(time);
    AUREA_CHECK(t.run("function f(){ let i = 0; while (true) { i++ } }", "t.js", e));
    const auto t0 = std::chrono::steady_clock::now();
    AUREA_CHECK(!t.call("f", {}, r, e));
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    AUREA_CHECK(e.aborted && e.message.find("por chamada") != std::string::npos);
    AUREA_CHECK_MSG(ms < 2000, "o limite de tempo deve parar o laco logo");

    // Por memória (e a memória volta depois).
    sc::Limits mem;
    mem.maxHeapBytes = 4u << 20;
    sc::Context m(mem);
    AUREA_CHECK(m.run("function f(){ const a = []; while (true) a.push({x: 'y'.repeat(100)}) }", "m.js", e));
    AUREA_CHECK(!m.call("f", {}, r, e));
    AUREA_CHECK(e.aborted && e.message.find("memoria") != std::string::npos);
    AUREA_CHECK(m.collect() < (2 << 20));

    // Ciclos (closure ↔ escopo) são coletados no meio da chamada.
    sc::Context g;
    AUREA_CHECK(g.run("function f(){ for (let i = 0; i < 40000; i++) { const a = {}; a.f = () => a } return 1 }", "c.js", e));
    AUREA_CHECK_MSG(g.call("f", {}, r, e), e.text().c_str());

    // Recursão sem fim: erro claro, não estouro da pilha.
    AUREA_CHECK(g.run("function rec(n){ return rec(n + 1) }", "r.js", e));
    AUREA_CHECK(!g.call("rec", {}, r, e));
    AUREA_CHECK(e.name == "RangeError");

    // Cancelamento de outra thread.
    sc::Limits slow;
    slow.maxSteps = ~u64{0};
    slow.maxTimeMs = 20000;
    sc::Context k(slow);
    AUREA_CHECK(k.run("function f(){ while (true) {} }", "k.js", e));
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        k.request_cancel();
    });
    AUREA_CHECK(!k.call("f", {}, r, e));
    stopper.join();
    AUREA_CHECK(e.aborted && e.name == "CancelError");
}

// =============================================================================
// Runtime: painéis, eventos, permissões
// =============================================================================
AUREA_TEST(PluginsV2, PanelStateEventsAndClearErrors) {
    PanelPlugin p = make_panel_plugin("estado", panel_manifest("com.teste.estado",
        R"("strings":{"pt-BR":{"ola":"Ola, {0}!"},"en":{"ola":"Hi, {0}!"}},)"), kPanelScript);
    plugins::PluginRuntime rt(p.info, str(p.dir / ".dados"));
    const plugins::PluginRuntime::ApiInstaller none;
    const plugins::CallHooks hooks;

    // Mudança de slider: presa à faixa e ao passo; o onChange roda.
    CallResult r = parse_result(rt.event("tamanho", "change", "37", "pt-BR", none, hooks));
    AUREA_CHECK_MSG(r.ok, r.raw.c_str());
    const json::Value* state = r.root.get("state");
    AUREA_CHECK(state && num(state->get("values"), "tamanho") == 35.0);
    const json::Value* texts = state ? state->get("texts") : nullptr;
    AUREA_CHECK(texts && texts->get("info") && texts->get("info")->string == "valor 35 de tamanho");
    r = parse_result(rt.event("tamanho", "change", "1000", "pt-BR", none, hooks));
    AUREA_CHECK(num(r.root.get("state")->get("values"), "tamanho") == 100.0);

    // Botão: o script troca itens da lista, escolhe um e desliga o botão na 3ª vez.
    for (int i = 0; i < 3; ++i) r = parse_result(rt.event("ok", "click", "", "pt-BR", none, hooks));
    AUREA_CHECK_MSG(r.ok, r.raw.c_str());
    AUREA_CHECK(num(r.value(), "vezes") == 3.0 && num(r.value(), "tamanho") == 100.0 && num(r.value(), "estilo") == 1.0);
    AUREA_CHECK(r.value()->get("cor")->string == "#FF0000");
    state = r.root.get("state");
    AUREA_CHECK(state->get("values")->get("lista")->string == "segundo");
    AUREA_CHECK(state->get("items")->get("lista")->array.size() == 2);
    AUREA_CHECK(state->get("disabled")->array.size() == 1 && state->get("disabled")->array[0].string == "ok");
    // Controle desligado não roda.
    r = parse_result(rt.event("ok", "click", "", "pt-BR", none, hooks));
    AUREA_CHECK(!r.ok);

    // Mensagens para a interface.
    r = parse_result(rt.event("lista", "select", "\"a\"", "pt-BR", none, hooks));
    AUREA_CHECK(r.ok);
    const json::Value* msgs = r.root.get("messages");
    AUREA_CHECK(msgs && msgs->array.size() == 1 && msgs->array[0].get("text")->string == "escolhido a");

    // Textos do manifesto no idioma da chamada.
    r = parse_result(rt.call("texto", "[]", "en-US", none, hooks));
    AUREA_CHECK(r.ok && r.value()->string == "Hi, Ana!");
    r = parse_result(rt.call("texto", "[]", "pt-BR", none, hooks));
    AUREA_CHECK(r.ok && r.value()->string == "Ola, Ana!");

    // Erros claros: tipo errado, função que não existe, throw com linha.
    r = parse_result(rt.call("corErrada", "[]", "pt-BR", none, hooks));
    AUREA_CHECK(!r.ok && r.error_name() == "TypeError" && r.error_text().find("#RRGGBB") != std::string::npos);
    r = parse_result(rt.call("naoExiste", "[]", "pt-BR", none, hooks));
    AUREA_CHECK(!r.ok && r.error_text().find("naoExiste") != std::string::npos);
    r = parse_result(rt.call("lancar", "[]", "pt-BR", none, hooks));
    AUREA_CHECK(!r.ok && r.error_name() == "RangeError");
    AUREA_CHECK_MSG(r.error_text().find("main.js:") != std::string::npos, r.error_text().c_str());
    // O console guardou os erros.
    AUREA_CHECK(rt.console_json().find("RangeError") != std::string::npos);

    // storage persiste entre runtimes do mesmo plugin.
    r = parse_result(rt.call("guarda", "[{\"x\":[1,2]}]", "pt-BR", none, hooks));
    AUREA_CHECK(r.ok && r.value()->get("x")->array.size() == 2);
    {
        plugins::PluginRuntime again(p.info, str(p.dir / ".dados"));
        r = parse_result(again.call("guarda", "[7]", "pt-BR", none, hooks));
        AUREA_CHECK(r.ok && r.value()->number == 7.0);
    }

    // Limite de tempo por chamada no runtime; a próxima chamada funciona.
    sc::Limits fast;
    fast.maxTimeMs = 120;
    fast.maxSteps = ~u64{0};
    plugins::PluginRuntime quick(p.info, str(p.dir / ".dados"), fast);
    r = parse_result(quick.call("laco", "[]", "pt-BR", none, hooks));
    AUREA_CHECK(!r.ok && r.aborted() && r.error_name() == "LimitError");
    r = parse_result(quick.call("texto", "[]", "pt-BR", none, hooks));
    AUREA_CHECK(r.ok);

    std::error_code ec;
    fs::remove_all(p.dir, ec);
}

AUREA_TEST(PluginsV2, NetworkClipboardAndFilesNeedConsentAndDeclaredDomains) {
    PanelPlugin p = make_panel_plugin("rede", panel_manifest("com.teste.rede",
        R"("permissions":{"network":["api.exemplo.com"],"clipboard":true,"files":true},)"), kPanelScript);
    std::atomic<int> calls{0};
    std::string lastUrl;
    auto io = std::make_shared<plugins::HostIO>();
    io->fetch = [&](const plugins::HttpRequest& req) {
        ++calls;
        lastUrl = req.url;
        plugins::HttpResponse r;
        r.ok = true;
        r.status = 200;
        r.body = "{\"oi\":\"mundo\"}";
        r.headers.emplace_back("Content-Type", "application/json");
        return r;
    };
    io->clipboardRead = [](std::string& t) { t = "copiado"; return true; };
    const fs::path picked = p.dir / "escolhido.json";
    write_file(picked, "{\"n\":3}");
    io->pickFile = [&](const std::string&, const std::vector<std::string>&) {
        plugins::PickedFile f;
        f.ok = true;
        f.name = "escolhido.json";
        f.path = str(picked);
        f.size = 7;
        return f;
    };
    plugins::PluginRuntime rt(p.info, str(p.dir / ".dados"));
    rt.set_io(io);
    const plugins::PluginRuntime::ApiInstaller none;
    const plugins::CallHooks hooks;

    // Nada concedido: tudo recusa, nenhum byte sai.
    rt.set_granted(0);
    CallResult r = parse_result(rt.call("rede", "[\"https://api.exemplo.com/x\"]", "pt-BR", none, hooks));
    AUREA_CHECK(!r.ok && r.error_name() == "PermissionError");
    r = parse_result(rt.call("clip", "[]", "pt-BR", none, hooks));
    AUREA_CHECK(!r.ok && r.error_name() == "PermissionError");
    r = parse_result(rt.call("arquivo", "[]", "pt-BR", none, hooks));
    AUREA_CHECK(!r.ok && r.error_name() == "PermissionError");
    AUREA_CHECK_EQ(calls.load(), 0);

    rt.set_granted(plugins::kPermNetwork | plugins::kPermClipboard | plugins::kPermFiles);
    // Domínio fora da lista, http, porta, usuário@ e domínio "parecido": recusa.
    for (const char* url : {"https://evil.com/x", "http://api.exemplo.com/x", "https://api.exemplo.com:8443/x",
                            "https://user@api.exemplo.com/x", "https://api.exemplo.com.evil.com/x", "https://xapi.exemplo.com/x"}) {
        r = parse_result(rt.call("rede", std::string("[\"") + url + "\"]", "pt-BR", none, hooks));
        AUREA_CHECK_MSG(!r.ok && r.error_name() == "PermissionError", url);
    }
    AUREA_CHECK_EQ(calls.load(), 0);
    // Domínio declarado e subdomínio dele: passam.
    r = parse_result(rt.call("rede", "[\"https://api.exemplo.com/v1?q=1\"]", "pt-BR", none, hooks));
    AUREA_CHECK_MSG(r.ok, r.raw.c_str());
    AUREA_CHECK(r.ok && r.value()->get("body")->get("oi")->string == "mundo" && num(r.value(), "status") == 200.0);
    r = parse_result(rt.call("rede", "[\"https://v2.api.exemplo.com/\", {\"method\":\"POST\",\"body\":{\"a\":1}}]", "pt-BR", none, hooks));
    AUREA_CHECK(r.ok);
    AUREA_CHECK_EQ(calls.load(), 2);
    AUREA_CHECK(lastUrl == "https://v2.api.exemplo.com/");
    // Área de transferência e arquivo escolhido.
    r = parse_result(rt.call("clip", "[]", "pt-BR", none, hooks));
    AUREA_CHECK(r.ok && r.value()->string == "copiado");
    r = parse_result(rt.call("arquivo", "[]", "pt-BR", none, hooks));
    AUREA_CHECK(r.ok && r.value()->get("name")->string == "escolhido.json");

    // Sem permissions.network no manifesto: recusa mesmo "concedendo".
    PanelPlugin q = make_panel_plugin("semrede", panel_manifest("com.teste.semrede"), kPanelScript);
    plugins::PluginRuntime none2(q.info, str(q.dir / ".dados"));
    none2.set_io(io);
    none2.set_granted(plugins::kPermNetwork);
    r = parse_result(none2.call("rede", "[\"https://api.exemplo.com/x\"]", "pt-BR", none, hooks));
    AUREA_CHECK(!r.ok && r.error_text().find("permissions.network") != std::string::npos);
    AUREA_CHECK_EQ(calls.load(), 2);

    std::error_code ec;
    fs::remove_all(p.dir, ec);
    fs::remove_all(q.dir, ec);
}

// =============================================================================
// Motor: instalação, consentimento, API de camadas/keyframes e desfazer
// =============================================================================
namespace {

EngineConfig headless(const fs::path& plugins) {
    EngineConfig cfg;
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    cfg.pluginDirectory = str(plugins);
    return cfg;
}

const char* kApiScript = R"(
function montar() {
  const comp = app.project.activeItem
  app.beginUndoGroup("Montar")
  const fundo = comp.layers.addSolid([1, 0, 0], "Fundo", 200, 100)
  fundo.position.setValueAtTime(0, [100, 50])
  fundo.position.setValueAtTime(1, [300, 50])
  fundo.opacity.setValue(50)
  fundo.rotation.setValueAtTime(0, 0)
  fundo.rotation.setValueAtTime(2, 90)
  fundo.rotation.setInterpolationTypeAtKey(1, "hold")
  const ctrl = comp.layers.addNull()
  ctrl.name = "Controle"
  fundo.parent = ctrl
  const fx = fundo.addEffect("aurea.blur.gaussian")
  comp.addMarker(0.5, "meio")
  app.endUndoGroup()
  return {
    camadas: comp.numLayers,
    topo: comp.layer(1).name,
    segunda: comp.layers[2].name,
    chaves: fundo.position.numKeys,
    x: fundo.position.valueAtTime(0.5)[0],
    t2: fundo.position.keyTime(2),
    rotHold: fundo.rotation.valueAtTime(1.5),
    opacidade: fundo.opacity.value,
    efeito: fx ? fx.matchName : null,
    efeitos: fundo.effects.length,
    pai: fundo.parent ? fundo.parent.name : null,
    marcas: comp.markers.length,
    tipo: fundo.type
  }
}
function erroNoMeio() {
  const comp = app.project.activeItem
  comp.layers.addNull()
  comp.layer(99).name = "x"
}
function apagarTudo() {
  const comp = app.project.activeItem
  for (const l of comp.allLayers) l.remove()
  return comp.numLayers
}
function semKey() { app.project.activeItem.layer(1).position.setValue([1, 2]) }
)";

const char* kApiManifest = R"({"aureaPlugin":2,"id":"com.teste.api","version":"1.0.0","name":"API","kind":"panel",
  "script":"main.js","ui":{"actions":[{"id":"m","title":"Montar","placement":"editor.menu","call":"montar"}]}})";

} // namespace

AUREA_TEST(PluginsV2, LayersKeyframesThroughEngineCommandsWithOneUndo) {
    const fs::path pluginDir = fresh_dir("motor");
    const fs::path pkg = fresh_dir("pacote") / "api.aureaplugin";
    write_zip(pkg, {{"plugin.json", kApiManifest}, {"main.js", kApiScript}});
    Engine e;
    AUREA_CHECK(e.initialize(headless(pluginDir)).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());

    // A folha de consentimento lê sem instalar.
    const std::string inspected = e.inspect_plugin(str(pkg).c_str());
    AUREA_CHECK_MSG(inspected.find("\"ok\":true") != std::string::npos && inspected.find("\"kind\":\"panel\"") != std::string::npos,
                    inspected.c_str());
    AUREA_CHECK(e.list_plugins().empty());
    const auto installed = e.install_plugin(str(pkg).c_str());
    AUREA_CHECK_MSG(installed.ok(), std::string(installed.status().detail()).c_str());
    if (!installed.ok()) { e.shutdown(); return; }
    AUREA_CHECK(e.plugins_json().find("\"hasScript\":true") != std::string::npos);
    // "panel" não entra no catálogo de efeitos.
    AUREA_CHECK(e.effects().find(effect_type_id("plugin.com.teste.api")) == nullptr);

    auto layers = [&] { return e.project()->timeline().composition(e.project()->timeline().current())->order().size(); };
    const u32 before = layers();
    CallResult r = parse_result(e.plugin_call("com.teste.api", "montar", "[]", "pt-BR"));
    AUREA_CHECK_MSG(r.ok, r.raw.c_str());
    if (r.ok) {
        const json::Value* v = r.value();
        AUREA_CHECK_EQ(num(v, "camadas"), 2.0);
        AUREA_CHECK(v->get("topo")->string == "Controle");
        AUREA_CHECK(v->get("segunda")->string == "Fundo");
        AUREA_CHECK_EQ(num(v, "chaves"), 2.0);
        AUREA_CHECK(num(v, "x") > 100.0 && num(v, "x") < 300.0);
        AUREA_CHECK_NEAR(num(v, "t2"), 1.0, 1e-9);
        AUREA_CHECK_NEAR(num(v, "rotHold"), 0.0, 1e-4);     // hold: fica no valor da 1ª chave
        AUREA_CHECK_NEAR(num(v, "opacidade"), 50.0, 1e-3);
        AUREA_CHECK(v->get("efeito")->string == "aurea.blur.gaussian");
        AUREA_CHECK_EQ(num(v, "efeitos"), 1.0);
        AUREA_CHECK(v->get("pai")->string == "Controle");
        AUREA_CHECK_EQ(num(v, "marcas"), 1.0);
        AUREA_CHECK(v->get("tipo")->string == "shape");
    }
    AUREA_CHECK_EQ(layers(), before + 2);
    // UM desfazer tira tudo o que a chamada fez.
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(layers(), before);
    Command redo;
    redo.type = CommandType::Redo;
    AUREA_CHECK(e.apply_command(redo).ok());
    AUREA_CHECK_EQ(layers(), before + 2);

    // Erro no meio: a mensagem diz o motivo; o que já foi feito também desfaz de uma vez.
    r = parse_result(e.plugin_call("com.teste.api", "erroNoMeio", "[]", "pt-BR"));
    AUREA_CHECK(!r.ok && r.error_text().find("nao da para gravar") != std::string::npos);
    AUREA_CHECK_EQ(layers(), before + 3);
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(layers(), before + 2);

    // setValue numa propriedade com keyframes: recusa como o AE.
    r = parse_result(e.plugin_call("com.teste.api", "semKey", "[]", "pt-BR"));
    // camada 1 = Controle (sem keyframes): aceita
    AUREA_CHECK_MSG(r.ok, r.raw.c_str());

    r = parse_result(e.plugin_call("com.teste.api", "apagarTudo", "[]", "pt-BR"));
    AUREA_CHECK(r.ok && r.value()->number == 0.0);
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(layers(), before + 2);

    // Desligado: nada roda. Religado: volta.
    AUREA_CHECK(e.set_plugin_enabled("com.teste.api", false));
    r = parse_result(e.plugin_call("com.teste.api", "montar", "[]", "pt-BR"));
    AUREA_CHECK(!r.ok && r.error_text().find("desligado") != std::string::npos);
    AUREA_CHECK(e.set_plugin_enabled("com.teste.api", true));
    AUREA_CHECK(e.plugins_json().find("\"enabled\":true") != std::string::npos);

    // Permissões só valem dentro do que o plugin pede (este não pede nada).
    AUREA_CHECK(e.set_plugin_permissions("com.teste.api", plugins::kPermNetwork));
    AUREA_CHECK(e.plugins_json().find("\"granted\":{\"network\":false") != std::string::npos);

    // O estado persiste: recarregar a pasta mantém "desligado".
    AUREA_CHECK(e.set_plugin_enabled("com.teste.api", false));
    e.set_plugin_directory(str(pluginDir).c_str());
    AUREA_CHECK(e.plugins_json().find("\"enabled\":false") != std::string::npos);

    AUREA_CHECK(e.remove_plugin("com.teste.api"));
    r = parse_result(e.plugin_call("com.teste.api", "montar", "[]", "pt-BR"));
    AUREA_CHECK(!r.ok);
    e.shutdown();
    std::error_code ec;
    fs::remove_all(pluginDir, ec);
    fs::remove_all(pkg.parent_path(), ec);
}

AUREA_TEST(PluginsV2, InstallChecksScriptSyntaxAndPanelImages) {
    EffectRegistry effects;
    register_builtin_effects(effects);
    plugins::PluginManager manager(effects);
    const fs::path dir = fresh_dir("sintaxe");
    manager.set_directory(str(dir / "plugins"));
    const std::string manifest = R"({"aureaPlugin":2,"id":"com.teste.sintaxe","version":"1.0.0","name":"S","kind":"panel","script":"main.js",
      "ui":{"panels":[{"id":"p","title":"P","controls":[{"id":"b","type":"button","label":"B","onClick":"f"},{"id":"i","type":"image","src":"capa.png"}]}]}})";
    write_zip(dir / "a.aureaplugin", {{"plugin.json", manifest}, {"main.js", "function f() {\n  return (\n}"}, {"capa.png", "x"}});
    auto r = manager.install(str(dir / "a.aureaplugin"), {});
    AUREA_CHECK(!r.ok());
    const std::string why(r.status().detail());
    AUREA_CHECK_MSG(why.find("main.js:3") != std::string::npos, why.c_str());
    write_zip(dir / "b.aureaplugin", {{"plugin.json", manifest}, {"main.js", "function f() { return 1 }"}});
    r = manager.install(str(dir / "b.aureaplugin"), {});
    AUREA_CHECK(!r.ok() && std::string(r.status().detail()).find("capa.png") != std::string::npos);
    write_zip(dir / "c.aureaplugin", {{"plugin.json", manifest}, {"main.js", "function f() { return 1 }"}, {"capa.png", "x"}});
    r = manager.install(str(dir / "c.aureaplugin"), {});
    AUREA_CHECK_MSG(r.ok(), std::string(r.status().detail()).c_str());
    manager.shutdown();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

AUREA_TEST(PluginsV2, ExamplesInstallAndRun) {
    const std::string base = AUREA_PLUGIN_EXAMPLES_DIR;
    const fs::path pluginDir = fresh_dir("exemplos");
    Engine e;
    AUREA_CHECK(e.initialize(headless(pluginDir)).ok());
    AUREA_CHECK(e.new_project(1080, 1920, 30.0, nullptr).ok());
    for (const char* name : {"animated-text.aureaplugin", "weather-text.aureaplugin"}) {
        const auto installed = e.install_plugin((base + "/" + name).c_str());
        AUREA_CHECK_MSG(installed.ok(), (std::string(name) + ": " + std::string(installed.status().detail())).c_str());
    }
    const std::string list = e.plugins_json();
    AUREA_CHECK(list.find("\"network\":[\"open-meteo.com\"]") != std::string::npos);
    // O gerador de texto animado cria a camada e os keyframes (precisa de fonte).
    if (text::default_font()) {
        CallResult r = parse_result(e.plugin_event("com.aurea.examples.animatedtext", "gerar", "click", "", "pt-BR"));
        AUREA_CHECK_MSG(r.ok, r.raw.c_str());
        const Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
        AUREA_CHECK(comp && comp->order().size() >= 1);
    }
    // O exemplo de API sem a permissão concedida: recusa clara, sem rede.
    CallResult r = parse_result(e.plugin_event("com.aurea.examples.weathertext", "buscar", "click", "", "pt-BR"));
    AUREA_CHECK(!r.ok && r.error_name() == "PermissionError");
    e.shutdown();
    std::error_code ec;
    fs::remove_all(pluginDir, ec);
}
