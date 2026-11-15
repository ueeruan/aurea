// =============================================================================
//  AE expressions (lote E, 2026-11-12; gap_system §11): o que faltava das
//  expressões do After Effects — conversão de cor, footage()/comp(), os
//  controles Menu suspenso / Camada / Ponto 3D, caminhos (points, tangentes,
//  pointOnPath, createPath e a expressão no caminho da máscara), sampleImage
//  (pela imagem pequena publicada na CPU), e a linguagem do dia a dia: funções
//  da pessoa, vetores longos, objetos, texto com crase, try/catch — sempre
//  dentro dos limites do sandbox.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/render/MaskRaster.hpp"
#include "aurea/text/FontManager.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/timeline/Timeline.hpp"
#include "aurea/vector/Vector.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace aurea;

namespace {

using TP = TrackProperty;

expr::StandaloneResult ev(const char* src, f64 time = 0.0, std::initializer_list<f64> value = {0.0}) {
    f64 v[4]{};
    u32 n = 0;
    for (f64 x : value) if (n < 4) v[n++] = x;
    return expr::evaluate_standalone(src, time, v, n, 30.0);
}

f64 num(const char* src, f64 time = 0.0) {
    const auto r = ev(src, time);
    if (!r.ok) std::printf("\n    [expr] '%s' falhou: %s (l%u c%u)\n", src, r.diag.message.c_str(), r.diag.line, r.diag.column);
    return r.ok ? r.v[0] : -12345.0;
}

/// Verdadeiro quando a expressão dá 1 (comparações de texto ficam dentro dela).
bool yes(const char* src) { return num(src) == 1.0; }

struct Rig {
    Timeline tl;
    Composition* comp = nullptr;
    Rig(u32 w = 1920, u32 h = 1080) {
        const CompositionId id = tl.create_composition("C", w, h, 30.0);
        tl.set_current(id);
        comp = tl.composition(id);
    }
    Layer* add(LayerKind kind, const char* name, LayerId* outId = nullptr) {
        const LayerId id = comp->add_layer(kind, name);
        Layer* l = comp->layer(id);
        l->start = FrameIndex{0};
        l->end = FrameIndex{300};
        if (outId) *outId = id;
        return l;
    }
    /// Valor da expressão `src` na trilha `p` da camada `l`, no quadro `frame`.
    f32 eval(Layer* l, TrackProperty p, const char* src, i64 frame = 0) {
        Track& t = l->tracks.get_or_create(p);
        t.expression = expr::compile(src);
        t.expressionEnabled = true;
        f32 v;
        {
            const expr::Scope scope(tl);
            v = t.value_or(FrameIndex{frame}, -12345.0f);
        }
        const auto err = t.expression->error();
        if (!err.ok) std::printf("\n    [expr] '%s' falhou: %s\n", src, err.message.c_str());
        t.expression.reset();
        return v;
    }
    std::string error(Layer* l, TrackProperty p, const char* src) {
        Track& t = l->tracks.get_or_create(p);
        t.expression = expr::compile(src);
        t.expressionEnabled = true;
        {
            const expr::Scope scope(tl);
            (void)t.value_or(FrameIndex{0}, 0.0f);
        }
        const std::string msg = t.expression->error().message;
        t.expression.reset();
        return msg;
    }
    /// O texto que a expressão dá (como a do texto-fonte), com `value` = "abc".
    std::string text(Layer* l, const char* src) {
        const auto te = expr::compile(src);
        const expr::Scope scope(tl);
        const expr::TextResult r = expr::evaluate_text(*te, *l, "abc", 0.0);
        return r.ok ? r.text : "<erro: " + r.diag.message + ">";
    }
};

u32 add_control(Layer& l, const char* key) {
    EffectInstance inst;
    inst.id = l.alloc_effect_id();
    inst.type = effect_type_id(key);
    initialize_instance(inst, *expr::builtin_effects().params(inst.type));
    l.effects.push_back(inst);
    return inst.id;
}

Mask square_mask(u32 id, const char* name, f32 side) {
    Mask m;
    m.id = id;
    m.name = name;
    m.closed = true;
    for (const Vec2 p : {Vec2{0, 0}, Vec2{side, 0}, Vec2{side, side}, Vec2{0, side}}) {
        MaskPoint q;
        q.position = p;
        m.points.push_back(q);
    }
    return m;
}

std::vector<MaskPoint> path_of(Rig& rig, const Mask& m, f64 frame) {
    std::vector<MaskPoint> out;
    const expr::Scope scope(rig.tl);
    mask::evaluate_path(m, frame, out);
    return out;
}

} // namespace

// -----------------------------------------------------------------------------
// 1. Cor
// -----------------------------------------------------------------------------
AUREA_TEST(ExprAe, ColorConversionsMatchAfterEffects) {
    auto r = ev("rgbToHsl([1, 0, 0, 1])");
    AUREA_CHECK(r.ok && r.count == 4);
    AUREA_CHECK_NEAR(r.v[0], 0.0, 1e-9); AUREA_CHECK_NEAR(r.v[1], 1.0, 1e-9);
    AUREA_CHECK_NEAR(r.v[2], 0.5, 1e-9); AUREA_CHECK_NEAR(r.v[3], 1.0, 1e-9);
    r = ev("rgbToHsl([0, 0, 1])");                       // sem alfa: 1
    AUREA_CHECK(r.ok && r.count == 4);
    AUREA_CHECK_NEAR(r.v[0], 2.0 / 3.0, 1e-9); AUREA_CHECK_NEAR(r.v[3], 1.0, 1e-9);
    r = ev("hslToRgb([1 / 3, 1, 0.5, 0.25])");
    AUREA_CHECK(r.ok && r.count == 4);
    AUREA_CHECK_NEAR(r.v[0], 0.0, 1e-9); AUREA_CHECK_NEAR(r.v[1], 1.0, 1e-9);
    AUREA_CHECK_NEAR(r.v[2], 0.0, 1e-9); AUREA_CHECK_NEAR(r.v[3], 0.25, 1e-9);
    r = ev("hslToRgb(rgbToHsl([0.2, 0.4, 0.6, 0.5]))");   // ida e volta
    AUREA_CHECK(r.ok);
    AUREA_CHECK_NEAR(r.v[0], 0.2, 1e-9); AUREA_CHECK_NEAR(r.v[1], 0.4, 1e-9);
    AUREA_CHECK_NEAR(r.v[2], 0.6, 1e-9); AUREA_CHECK_NEAR(r.v[3], 0.5, 1e-9);
    r = ev("rgbToHsl([0.5, 0.5, 0.5, 1])");               // cinza: sem matiz nem saturação
    AUREA_CHECK(r.ok && r.v[0] == 0.0 && r.v[1] == 0.0);
    AUREA_CHECK_NEAR(r.v[2], 0.5, 1e-12);
    r = ev("hexToRgb(\"FF8000\")");
    AUREA_CHECK(r.ok && r.count == 4);
    AUREA_CHECK_NEAR(r.v[0], 1.0, 1e-9); AUREA_CHECK_NEAR(r.v[1], 128.0 / 255.0, 1e-9);
    AUREA_CHECK_NEAR(r.v[2], 0.0, 1e-9); AUREA_CHECK_NEAR(r.v[3], 1.0, 1e-9);
    r = ev("hexToRgb(\"#0f0\")");
    AUREA_CHECK(r.ok && r.v[0] == 0.0 && r.v[1] == 1.0 && r.v[2] == 0.0 && r.v[3] == 1.0);
    r = ev("hexToRgb(\"0x336699CC\")");                    // quarteto: com alfa
    AUREA_CHECK(r.ok);
    AUREA_CHECK_NEAR(r.v[0], 0.2, 1e-9); AUREA_CHECK_NEAR(r.v[2], 0.6, 1e-9); AUREA_CHECK_NEAR(r.v[3], 0.8, 1e-9);
    r = ev("hexToRgb(0xFF0000)");                          // número também vale
    AUREA_CHECK(r.ok && r.v[0] == 1.0 && r.v[1] == 0.0);
    AUREA_CHECK(!ev("hexToRgb(\"xyz\")").ok);
    AUREA_CHECK(!ev("hexToRgb(\"12345\")").ok);
    AUREA_CHECK(!ev("rgbToHsl(5)").ok);
    // Os vizinhos pedidos no mesmo item: length(a, b) e graus <-> radianos.
    AUREA_CHECK_NEAR(num("length([1, 1], [4, 5])"), 5.0, 1e-12);
    AUREA_CHECK_NEAR(num("length([0, 0, 0], [2, 3, 6])"), 7.0, 1e-12);
    AUREA_CHECK_NEAR(num("radiansToDegrees(degreesToRadians(123))"), 123.0, 1e-9);
    AUREA_CHECK_NEAR(num("0xFF + 0x10"), 271.0, 1e-12);
    std::printf("rgbToHsl/hslToRgb/hexToRgb (3, 4, 6, 8 digitos, #, 0x, numero), length(a,b), graus/radianos; ");
}

// -----------------------------------------------------------------------------
// 6. Linguagem: funções, vetores longos, objetos, texto
// -----------------------------------------------------------------------------
AUREA_TEST(ExprAe, UserFunctionsArrowsClosuresAndRecursion) {
    AUREA_CHECK_NEAR(num("function sq(x) { return x * x } sq(7)"), 49.0, 1e-12);
    AUREA_CHECK_NEAR(num("var r = twice(4);\nfunction twice(x) { return x * 2 }\nr"), 8.0, 1e-12);   // içada
    AUREA_CHECK_NEAR(num("function fact(n) { return n <= 1 ? 1 : n * fact(n - 1) } fact(10)"), 3628800.0, 1e-6);
    AUREA_CHECK_NEAR(num("function fib(n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2) } fib(12)"), 144.0, 1e-9);
    AUREA_CHECK_NEAR(num("var k = 3; var f = x => x * k; f(5)"), 15.0, 1e-12);                     // lê a variável de fora
    AUREA_CHECK_NEAR(num("const f = (a, b = 10) => { return a + b }; f(1) + f(1, 2)"), 14.0, 1e-12);
    AUREA_CHECK_NEAR(num("var f = function (a, b) { return a - b }; f(9, 4)"), 5.0, 1e-12);
    AUREA_CHECK_NEAR(num("function sum(...xs) { var s = 0; for (const x of xs) s += x; return s } sum(1, 2, 3, 4, 5)"), 15.0, 1e-12);
    AUREA_CHECK_NEAR(num("Math.max(...[3, 9, 4]) + Math.min(...[3, 9, 4])"), 12.0, 1e-12);
    AUREA_CHECK_NEAR(num("function apply(fn, v) { return fn(v) } apply(x => x + 1, 41)"), 42.0, 1e-12);
    AUREA_CHECK_NEAR(num("function outer(a) { function inner(b) { return a + b } return inner(10) } outer(5)"), 15.0, 1e-12);
    // Função sem return = undefined; parâmetro que não veio = undefined.
    AUREA_CHECK(yes("function f() { } f() === undefined ? 1 : 0"));
    AUREA_CHECK(yes("function f(a, b) { return b === undefined ? 1 : 0 } f(1)"));
    // Variável da função não vaza nem pisa na de fora; a de fora é lida e escrita.
    AUREA_CHECK_NEAR(num("var x = 1; function f() { var x = 50; return x } f() + x"), 51.0, 1e-12);
    AUREA_CHECK_NEAR(num("var total = 0; function add(n) { total += n } add(2); add(3); total"), 5.0, 1e-12);
    // Sombra de um nome embutido por uma função da pessoa (comum em expressão colada).
    AUREA_CHECK_NEAR(num("function clamp(v, lo, hi) { return 77 } clamp(1, 2, 3)"), 77.0, 1e-12);
    // Vetores do AE continuam valendo dentro das funções.
    auto r = ev("function mid(a, b) { return (a + b) / 2 } mid([0, 10], [100, 30])");
    AUREA_CHECK(r.ok && r.count == 2 && r.v[0] == 50.0 && r.v[1] == 20.0);
    r = ev("function wobble(t) { return [Math.sin(t) * 10, Math.cos(t) * 10] } value + wobble(0)", 0.0, {100.0, 200.0});
    AUREA_CHECK(r.ok && r.count == 2);
    AUREA_CHECK_NEAR(r.v[0], 100.0, 1e-9); AUREA_CHECK_NEAR(r.v[1], 210.0, 1e-9);
    // Uma função que sai do escopo em que nasceu é recusada com mensagem (sem travar).
    r = ev("function counter() { var c = 0; return function () { c += 1; return c } } var next = counter(); next()");
    AUREA_CHECK(!r.ok && r.diag.message.find("escopo") != std::string::npos);
    AUREA_CHECK(!ev("var f = 5; f(1)").ok);
    AUREA_CHECK(!ev("function f(a, a) { return a }").ok);
    std::printf("function/arrow/closure sobre o escopo vivo, padrao, ...resto, recursao, icamento; ");
}

AUREA_TEST(ExprAe, LongArraysWithTheCommonMethods) {
    AUREA_CHECK_NEAR(num("var a = [1, 2, 3, 4, 5, 6]; a.length"), 6.0, 1e-12);
    AUREA_CHECK_NEAR(num("var a = [1, 2, 3, 4, 5, 6]; a.push(7); a[6] + a.length"), 14.0, 1e-12);
    AUREA_CHECK_NEAR(num("var a = []; for (var i = 0; i < 100; i++) a.push(i * 2); a[99] + a.length"), 298.0, 1e-12);
    AUREA_CHECK(yes("[1, 2, 3, 4, 5, 6].slice(1, 3).join(\"-\") == \"2-3\" ? 1 : 0"));
    AUREA_CHECK(yes("[1, 2, 3, 4, 5, 6].slice(-2).join() == \"5,6\" ? 1 : 0"));
    AUREA_CHECK_NEAR(num("[5, 6, 7, 8, 9].indexOf(8) + [5, 6, 7, 8, 9].indexOf(1)"), 2.0, 1e-12);
    AUREA_CHECK_NEAR(num("[1, 2, 3, 4, 5, 6].map(x => x * 2).filter(x => x > 4).reduce((s, x) => s + x, 0)"), 36.0, 1e-12);
    AUREA_CHECK_NEAR(num("[1, 2, 3, 4, 5].reduce((s, x) => s + x)"), 15.0, 1e-12);
    AUREA_CHECK_NEAR(num("[4, 8, 15, 16, 23, 42].find(x => x > 15) + [4, 8, 15, 16, 23, 42].findIndex(x => x > 15)"), 19.0, 1e-12);
    AUREA_CHECK(yes("[1, 2, 3, 4, 5].some(x => x > 4) && [1, 2, 3, 4, 5].every(x => x > 0) && ![1, 2, 3, 4, 5].includes(9) ? 1 : 0"));
    AUREA_CHECK_NEAR(num("var s = 0; [1, 2, 3, 4, 5].forEach((x, i) => { s += x * i }); s"), 40.0, 1e-12);
    AUREA_CHECK_NEAR(num("var a = [1, 2, 3, 4, 5]; a.pop() + a.shift() + a.length"), 9.0, 1e-12);
    AUREA_CHECK(yes("var a = [3, 4, 5, 6, 7]; a.unshift(1, 2); a.reverse(); a.join(\"\") == \"7654321\" ? 1 : 0"));
    AUREA_CHECK(yes("var a = [1, 2, 3, 4, 5]; var cut = a.splice(1, 2, 9, 9, 9); a.join() == \"1,9,9,9,4,5\" && cut.join() == \"2,3\" ? 1 : 0"));
    AUREA_CHECK(yes("[1, 2, 3, 4, 5].concat([6, 7], 8).length == 8 ? 1 : 0"));
    AUREA_CHECK(yes("[10, 9, 1, 100, 25].sort((a, b) => a - b).join() == \"1,9,10,25,100\" ? 1 : 0"));
    AUREA_CHECK(yes("[10, 9, 1, 100, 25].sort().join() == \"1,10,100,25,9\" ? 1 : 0"));          // sem função: como texto (JS)
    AUREA_CHECK(yes("[\"b\", \"a\", \"c\"].sort().join(\"\") == \"abc\" ? 1 : 0"));
    AUREA_CHECK(yes("[[1, 2], [3, 4], [5, 6]].flat().length == 6 && [1, 2, 3, 4, 5].at(-1) == 5 ? 1 : 0"));
    AUREA_CHECK(yes("Array.isArray([1, 2, 3, 4, 5]) && Array.isArray([1, 2]) && !Array.isArray(3) ? 1 : 0"));
    AUREA_CHECK(yes("var a = [1, 2, 3, 4, 5]; a[9] === undefined && a[7] == null ? 1 : 0"));
    // O vetor-valor do AE (até 4 números) vira vetor de verdade quando precisa.
    AUREA_CHECK_NEAR(num("var v = [1, 2]; v.push(3); v.push(4); v.push(5); v.length"), 5.0, 1e-12);
    auto r = ev("[1, 2, 3].map(x => x + 1)");
    AUREA_CHECK(r.ok && r.count == 3 && r.v[0] == 2.0 && r.v[2] == 4.0);
    r = ev("var p = []; p.push(10); p.push(20); p + [1, 1]");             // entra na conta como vetor
    AUREA_CHECK(r.ok && r.count == 2 && r.v[0] == 11.0 && r.v[1] == 21.0);
    r = ev("var v = [1, 2]; v[0] = 5; v[1] += 10; v");
    AUREA_CHECK(r.ok && r.count == 2 && r.v[0] == 5.0 && r.v[1] == 12.0);
    AUREA_CHECK_NEAR(num("var a = [0, 0, 0, 0, 0]; a[4] = 9; a[0] += 2; a[1]++; a[4] + a[0] + a[1]"), 12.0, 1e-12);
    AUREA_CHECK_NEAR(num("var g = [[1, 2], [3, 4], [5, 6]]; g[2][1] = 60; g[2][1] + g[0][0]"), 61.0, 1e-12);
    AUREA_CHECK_NEAR(num("var a = [1, 2, 3, 4, 5]; var b = [...a, 6, ...[7, 8]]; b.length + b[7]"), 16.0, 1e-12);
    AUREA_CHECK_NEAR(num("const [a, b] = [3, 4]; const [, , c] = [1, 2, 30]; a * b + c"), 42.0, 1e-12);
    AUREA_CHECK_NEAR(num("var s = 0; for (const x of [1, 2, 3, 4, 5]) { if (x == 4) continue; s += x } s"), 11.0, 1e-12);
    AUREA_CHECK_NEAR(num("var s = 0; for (var i in [7, 8, 9, 10, 11]) s += i; s"), 10.0, 1e-12);
    // O resultado de uma propriedade continua precisando de 1 a 4 números.
    AUREA_CHECK(!ev("[1, 2, 3, 4, 5]").ok);
    AUREA_CHECK(!ev("[1, \"a\"]").ok);
    std::printf("push/pop/shift/unshift/slice/splice/concat/join/indexOf/includes/sort/reverse/map/filter/reduce/find/some/every/forEach/flat, a[i] = v, [...a], for-of; ");
}

AUREA_TEST(ExprAe, ObjectLiteralsAndPropertyAccess) {
    AUREA_CHECK_NEAR(num("var o = {a: 1, \"b c\": 2, n: {x: 5}}; o.z = 10; o.a + o[\"b c\"] + o.n.x + o.z"), 18.0, 1e-12);
    AUREA_CHECK_NEAR(num("var w = 3; var o = {w, h: w * 2}; o.w + o.h"), 9.0, 1e-12);
    AUREA_CHECK(yes("var o = {a: 1}; o.nope === undefined && o.hasOwnProperty(\"a\") && !o.hasOwnProperty(\"b\") ? 1 : 0"));
    AUREA_CHECK_NEAR(num("var o = {a: 1, b: 2, c: 3}; Object.keys(o).length + Object.values(o).reduce((s, x) => s + x, 0)"), 9.0, 1e-12);
    AUREA_CHECK(yes("Object.keys({x: 1, y: 2}).join() == \"x,y\" && Object.entries({x: 1})[0][0] == \"x\" ? 1 : 0"));
    AUREA_CHECK_NEAR(num("var o = {n: 1}; o.n += 5; o.n++; o[\"n\"] *= 2; o.n"), 14.0, 1e-12);
    AUREA_CHECK_NEAR(num("var o = {a: 1, b: 2}; var s = 0; for (var k in o) s += o[k]; s"), 3.0, 1e-12);
    AUREA_CHECK_NEAR(num("const {width, height: h} = {width: 4, height: 5}; width * h"), 20.0, 1e-12);
    AUREA_CHECK_NEAR(num("var o = {area(w, h) { return w * h }, half: x => x / 2}; o.area(6, 4) + o.half(10)"), 29.0, 1e-12);
    AUREA_CHECK_NEAR(num("var key = \"dyn\"; var o = {[key + \"1\"]: 7}; o.dyn1"), 7.0, 1e-12);
    // Tabela de consulta: o uso mais comum em expressão colada.
    AUREA_CHECK_NEAR(num("var sizes = {small: 50, medium: 100, large: 200}; var pick = \"medium\"; sizes[pick]"), 100.0, 1e-12);
    AUREA_CHECK_NEAR(num("var pts = [{x: 1, y: 2}, {x: 10, y: 20}]; pts[1].x + pts[0].y"), 12.0, 1e-12);
    // Objeto é referência; vetor de pontos dentro dele também.
    AUREA_CHECK_NEAR(num("var a = {n: 1}; var b = a; b.n = 9; a.n"), 9.0, 1e-12);
    AUREA_CHECK(!ev("var o = {a: 1}; o").ok);                 // objeto não é valor de propriedade
    AUREA_CHECK(!ev("var n = 5; n.x = 1").ok);
    AUREA_CHECK(!ev("var u; u.x").ok);
    std::printf("{chave: valor}, abreviada, calculada, metodo, o.k / o[\"k\"], atribuicao, Object.keys/values/entries; ");
}

AUREA_TEST(ExprAe, StringMethodsTemplatesAndTheRestOfEverydayJs) {
    AUREA_CHECK_NEAR(num("\"a,b,c\".split(\",\").length"), 3.0, 1e-12);
    AUREA_CHECK(yes("\"a,b,c\".split(\",\")[1] == \"b\" ? 1 : 0"));
    AUREA_CHECK(yes("\"abc\".split(\"\").reverse().join(\"\") == \"cba\" ? 1 : 0"));
    AUREA_CHECK(yes("\"one two  three\".split(\" \").length == 4 && \"x\".split(\",\").length == 1 ? 1 : 0"));
    AUREA_CHECK(yes("\"a-b-c\".split(\"-\", 2).join(\"+\") == \"a+b\" ? 1 : 0"));
    AUREA_CHECK_NEAR(num("\"hello\".indexOf(\"l\") + \"hello\".lastIndexOf(\"l\") + \"hello\".indexOf(\"z\")"), 4.0, 1e-12);
    AUREA_CHECK(yes("\"hello\".charAt(1) == \"e\" && \"hello\"[4] == \"o\" && \"hello\".at(-1) == \"o\" ? 1 : 0"));
    AUREA_CHECK(yes("\"  x y \".trim() == \"x y\" && \"  x\".trimStart() == \"x\" && \"x  \".trimEnd() == \"x\" ? 1 : 0"));
    AUREA_CHECK(yes("\"7\".padStart(3, \"0\") == \"007\" && \"7\".padEnd(3, \"-\") == \"7--\" ? 1 : 0"));
    AUREA_CHECK(yes("\"AbC\".toLowerCase() == \"abc\" && \"AbC\".toUpperCase() == \"ABC\" ? 1 : 0"));
    AUREA_CHECK_NEAR(num("parseInt(\"42px\") + parseFloat(\"0.5em\") + parseInt(\"ff\", 16)"), 297.5, 1e-12);
    AUREA_CHECK(yes("\"a\".concat(\"b\", 1) == \"ab1\" && String.fromCharCode(65, 66) == \"AB\" ? 1 : 0"));
    // Texto com crase.
    AUREA_CHECK(yes("var n = 3; `n=${n * 2}!` == \"n=6!\" ? 1 : 0"));
    AUREA_CHECK(yes("var who = \"Aurea\"; `${who} ${1 + 1}.${[1, 2].length}` == \"Aurea 2.2\" ? 1 : 0"));
    AUREA_CHECK(yes("`sem nada` == \"sem nada\" && `` == \"\" ? 1 : 0"));
    AUREA_CHECK(yes("var o = {a: {b: 2}}; `v${o.a.b}${`[${o.a.b + 1}]`}` == \"v2[3]\" ? 1 : 0"));
    AUREA_CHECK(!ev("`aberto ${1 + 1").ok);
    // typeof, null/undefined, ??, switch, do-while, try/catch/finally, throw.
    AUREA_CHECK(yes("typeof 1 == \"number\" && typeof \"s\" == \"string\" && typeof undefined == \"undefined\" && typeof (x => x) == \"function\" && typeof [1, 2, 3, 4, 5] == \"object\" ? 1 : 0"));
    AUREA_CHECK(yes("null == undefined && (null ?? 5) == 5 && (0 ?? 5) == 0 && !null ? 1 : 0"));
    AUREA_CHECK_NEAR(num("var r = 0; switch (2) { case 1: r = 10; break; case 2: r = 20; case 3: r += 1; break; default: r = 99 } r"), 21.0, 1e-12);
    AUREA_CHECK_NEAR(num("var r = 0; switch (\"zz\") { case \"a\": r = 1; break; default: r = 7 } r"), 7.0, 1e-12);
    AUREA_CHECK_NEAR(num("var i = 0; do { i++ } while (i < 5); i"), 5.0, 1e-12);
    AUREA_CHECK_NEAR(num("var j = 10; for (var i = 0; i < 3; i++, j--) { } j"), 7.0, 1e-12);
    AUREA_CHECK_NEAR(num("var x = 10; x %= 4; x"), 2.0, 1e-12);
    AUREA_CHECK_NEAR(num("try { thisComp.layer(\"nao existe\").position } catch (e) { 7 }"), 7.0, 1e-12);
    AUREA_CHECK(yes("try { throw \"boom\" } catch (e) { e == \"boom\" ? 1 : 0 }"));
    AUREA_CHECK(yes("var m = \"\"; try { [1, 2][9] } catch (err) { m = err.message } m.length > 0 ? 1 : 0"));
    AUREA_CHECK_NEAR(num("var x = 0; try { x = 1 } finally { x += 10 } x"), 11.0, 1e-12);
    AUREA_CHECK_NEAR(num("function safe(fn, alt) { try { return fn() } catch (e) { return alt } } safe(() => thisComp.width, 123)"), 123.0, 1e-12);
    auto r = ev("throw \"minha mensagem\"");
    AUREA_CHECK(!r.ok && r.diag.message.find("minha mensagem") != std::string::npos);
    AUREA_CHECK(yes("isNaN(parseInt(\"x\")) && isFinite(3) && !isFinite(1 / 0) && Number.isInteger(4) ? 1 : 0"));
    AUREA_CHECK_NEAR(num("Math.sign(-3) + Math.trunc(2.9) + Math.hypot(3, 4) + Math.cbrt(27) + Math.log10(1000) + Math.log2(8)"), -1 + 2 + 5 + 3 + 3 + 3, 1e-9);
    // No texto-fonte: o resultado é texto, vetor vira "a,b,c".
    Rig rig;
    Layer* text = rig.add(LayerKind::Text, "T");
    AUREA_CHECK_EQ(rig.text(text, "value.split(\"\").map(c => c.toUpperCase()).join(\"-\")"), std::string("A-B-C"));
    AUREA_CHECK_EQ(rig.text(text, "`${value}:${value.length}`"), std::string("abc:3"));
    AUREA_CHECK_EQ(rig.text(text, "var parts = [\"a\", \"b\"]; parts.push(\"c\", \"d\", \"e\"); parts"), std::string("a,b,c,d,e"));
    AUREA_CHECK_EQ(rig.text(text, "\"linha 1\\rlinha 2\""), std::string("linha 1\nlinha 2"));   // \r quebra a linha, como no AE
    std::printf("split/indexOf/charAt/trim/padStart/toLowerCase/parseInt/parseFloat, crase, typeof, ??, switch, do-while, try/catch/finally/throw; ");
}

AUREA_TEST(ExprAe, SandboxStillBoundsFunctionsArraysAndStrings) {
    const auto t0 = std::chrono::steady_clock::now();
    auto r = ev("function f() { return f() } f()");                      // recursão sem fim
    AUREA_CHECK(!r.ok && (r.diag.message.find("fundo") != std::string::npos || r.diag.message.find("profundo") != std::string::npos));
    r = ev("var f = n => f(n + 1); f(0)");
    AUREA_CHECK(!r.ok);
    r = ev("function a() { return b() } function b() { return a() } a()");
    AUREA_CHECK(!r.ok);
    r = ev("function d(n) { return n == 0 ? 0 : 1 + d(n - 1) } d(20)");   // recursão comum cabe
    AUREA_CHECK(r.ok && r.v[0] == 20.0);
    AUREA_CHECK(!ev("function d(n) { return n == 0 ? 0 : 1 + d(n - 1) } d(5000)").ok);
    r = ev("var s = \"x\"; while (true) s = s + s");                    // texto que dobra
    AUREA_CHECK(!r.ok);
    r = ev("var a = []; while (true) a.push(1)");                         // vetor que cresce
    AUREA_CHECK(!r.ok && r.diag.message.find("limite") != std::string::npos);
    r = ev("var a = []; while (true) a.push(a)");
    AUREA_CHECK(!r.ok);
    r = ev("var a = [\"0123456789\"]; for (var i = 0; i < 40; i++) a = a.concat(a); a.length");   // dobra 40 vezes
    AUREA_CHECK(!r.ok);
    r = ev("var s = \"0123456789abcdef\"; var a = []; for (;;) a.push(s.repeat(4000))");
    AUREA_CHECK(!r.ok);
    r = ev("var a = []; a.push(a); var b = [a]; a.push(b); String(a).length");   // vetor dentro dele mesmo
    AUREA_CHECK(r.ok);
    r = ev("[5, 3, 8, 1, 9, 2, 7].sort(() => 1).length");                // comparação torta não derruba
    AUREA_CHECK(r.ok && r.v[0] == 7.0);
    r = ev("[5, 3, 8, 1, 9, 2, 7].sort((a, b) => { while (true) { } }).length");
    AUREA_CHECK(!r.ok && r.diag.message.find("limite") != std::string::npos);
    r = ev("[1, 2, 3, 4, 5].map(x => { while (true) { } })");
    AUREA_CHECK(!r.ok && r.diag.message.find("limite") != std::string::npos);
    r = ev("try { while (true) { } } catch (e) { 1 }");                   // catch não segura o limite
    AUREA_CHECK(!r.ok && r.diag.message.find("limite") != std::string::npos);
    r = ev("try { function f() { return f() } f() } catch (e) { 1 }");
    AUREA_CHECK(!r.ok);
    r = ev("var a = []; for (var i = 0; i < 20000; i++) a.push(i); var n = 0; while (a.length) { a.shift(); n++ } n");
    AUREA_CHECK(!r.ok);                                                   // o shift em laço cobra pelo tamanho
    r = ev("var o = {}; for (var i = 0; ; i++) o[\"k\" + i] = i");
    AUREA_CHECK(!r.ok);
    // Aninhamento absurdo de funções e de literais: erro de sintaxe, sem estourar a pilha.
    std::string nested;
    for (int i = 0; i < 400; ++i) nested += "(() => ";
    nested += "1";
    for (int i = 0; i < 400; ++i) nested += ")()";
    AUREA_CHECK(!ev(nested.c_str()).ok);
    std::string arr(3000, '[');
    arr += "1";
    arr += std::string(3000, ']');
    AUREA_CHECK(!ev(arr.c_str()).ok);
    std::string obj;
    for (int i = 0; i < 3000; ++i) obj += "{a:";
    obj += "1";
    obj += std::string(3000, '}');
    AUREA_CHECK(!ev(("var o = " + obj).c_str()).ok);
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    AUREA_CHECK(ms < 20000.0);
    std::printf("recursao/texto/vetor/objeto sem fim cortados pelos limites em %.0f ms no total, sem travar; ", ms);
}

// -----------------------------------------------------------------------------
// 2. footage() e comp()
// -----------------------------------------------------------------------------
AUREA_TEST(ExprAe, FootageAndCompByName) {
    struct Files { int calls = 0; } files;
    expr::register_footage_provider([](void* ctx, const Timeline*, std::string_view name, u64 asset, expr::FootageInfo& out) -> bool {
        ++static_cast<Files*>(ctx)->calls;
        if (name != "clipe.mp4" && !(name.empty() && asset == 77)) return false;
        out.name = "clipe.mp4";
        out.width = 1280;
        out.height = 720;
        out.duration = 12.5;
        out.frameDuration = 1.0 / 25.0;
        out.asset = 77;
        return true;
    }, &files);
    AUREA_CHECK_NEAR(num("footage(\"clipe.mp4\").width"), 1280.0, 1e-12);
    AUREA_CHECK_NEAR(num("var f = footage(\"clipe.mp4\"); f.height + f.duration"), 732.5, 1e-12);
    AUREA_CHECK_NEAR(num("footage(\"clipe.mp4\").frameDuration"), 0.04, 1e-12);
    AUREA_CHECK(yes("footage(\"clipe.mp4\").name == \"clipe.mp4\" ? 1 : 0"));
    auto r = ev("footage(\"outro.mov\").width");
    AUREA_CHECK(!r.ok && r.diag.message.find("outro.mov") != std::string::npos);
    AUREA_CHECK(!ev("footage(3)").ok);
    AUREA_CHECK(files.calls >= 5);

    Rig rig;
    const CompositionId otherId = rig.tl.create_composition("Outra", 800, 600, 24.0);
    Composition* other = rig.tl.composition(otherId);
    other->set_duration(FrameIndex{48});
    const LayerId farId = other->add_layer(LayerKind::Null, "Longe");
    other->layer(farId)->transform.position = Vec3{70.0f, 80.0f, 0.0f};
    (void)other->add_layer(LayerKind::Null, "Topo");
    Layer* probe = rig.add(LayerKind::Null, "Sonda");
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "comp(\"Outra\").width"), 800.0f, 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "comp(\"Outra\").height"), 600.0f, 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "comp(\"Outra\").duration"), 2.0f, 1e-4f);          // 48 quadros a 24
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "comp(\"Outra\").frameDuration * 1000"), 1000.0f / 24.0f, 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "comp(\"Outra\").numLayers"), 2.0f, 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "comp(\"Outra\").layer(\"Longe\").transform.position[1]"), 80.0f, 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "comp(\"Outra\").layer(2).position[0]"), 70.0f, 1e-3f);   // 1 = a de cima
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.frameDuration * 30"), 1.0f, 1e-5f);
    // Pré-composição: layer.source é a composição de dentro; camada com arquivo: o arquivo.
    Layer* pre = rig.add(LayerKind::Composition, "Pre");
    pre->nested.composition = otherId;
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.layer(\"Pre\").source.width"), 800.0f, 1e-3f);
    Layer* video = rig.add(LayerKind::Video, "Video");
    video->source = AssetId::unpack(77);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.layer(\"Video\").source.height"), 720.0f, 1e-3f);
    AUREA_CHECK(!rig.error(probe, TP::PositionX, "thisComp.layer(\"Sonda\").source.width").empty());
    AUREA_CHECK(!rig.error(probe, TP::PositionX, "comp(\"Nenhuma\").width").empty());
    expr::unregister_footage_provider(&files);
    AUREA_CHECK(!ev("footage(\"clipe.mp4\").width").ok);        // sem quem responda, erro com mensagem
    std::printf("footage(nome).width/height/duration/frameDuration/name, layer.source, comp(nome) com a taxa dela; ");
}

// -----------------------------------------------------------------------------
// 3. Controles: Menu suspenso, Camada, Ponto 3D
// -----------------------------------------------------------------------------
AUREA_TEST(ExprAe, DropdownLayerAnd3DPointControls) {
    const EffectRegistry& reg = expr::builtin_effects();
    AUREA_CHECK(reg.find(effect_type_id(effect_keys::kDropdownControl)) != nullptr);
    AUREA_CHECK(reg.find(effect_type_id(effect_keys::kLayerControl)) != nullptr);
    AUREA_CHECK(reg.find(effect_type_id(effect_keys::kPoint3DControl)) != nullptr);
    AUREA_CHECK(expr::is_menu_control(effect_type_id(effect_keys::kDropdownControl)));
    AUREA_CHECK(!expr::is_menu_control(effect_type_id(effect_keys::kSliderControl)));

    Rig rig;
    LayerId ctlId, targetId;
    Layer* target = rig.add(LayerKind::Shape, "Alvo", &targetId);
    target->transform.position = Vec3{300.0f, 400.0f, 0.0f};
    Layer* ctl = rig.add(LayerKind::Null, "Ctl", &ctlId);
    const u32 menu = add_control(*ctl, effect_keys::kDropdownControl);
    const u32 pick = add_control(*ctl, effect_keys::kLayerControl);
    const u32 p3 = add_control(*ctl, effect_keys::kPoint3DControl);
    Layer* probe = rig.add(LayerKind::Null, "Sonda");
    ctl = rig.comp->layer(ctlId);
    target = rig.comp->layer(targetId);
    EffectInstance* menuFx = ctl->find_effect(EffectId{menu, 0});
    EffectInstance* pickFx = ctl->find_effect(EffectId{pick, 0});
    EffectInstance* p3Fx = ctl->find_effect(EffectId{p3, 0});
    AUREA_CHECK(menuFx && pickFx && p3Fx);
    if (!menuFx || !pickFx || !p3Fx) return;

    // Menu: o motor guarda o índice (0..), a expressão lê 1.. como no After Effects.
    AUREA_CHECK_EQ(expr::menu_items(*menuFx).size(), static_cast<usize>(3));
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.layer(\"Ctl\").effect(\"Dropdown Menu Control\")(\"Menu\")"), 1.0f, 1e-4f);
    menuFx->params[0].constant.v[0] = 2.0f;
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.layer(\"Ctl\").effect(\"Dropdown Menu Control\")(\"Menu\")"), 3.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.layer(\"Ctl\").effect(\"Dropdown Menu Control\")(1).value"), 3.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.layer(\"Ctl\").effect(\"aurea.control.dropdown\")(\"menu\")"), 3.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(ctl, TP::PositionX, "effect(\"Controle de menu suspenso\")(\"Menu\") * 10"), 30.0f, 1e-4f);
    // O uso típico: trocar de valor pelo item.
    const char* pickBy = "var m = thisComp.layer(\"Ctl\").effect(\"Dropdown Menu Control\")(\"Menu\").value;\n"
                         "switch (m) { case 1: 100; break; case 2: 200; break; default: 300 }";
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, pickBy), 300.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "[10, 20, 30][thisComp.layer(\"Ctl\").effect(\"Dropdown Menu Control\")(\"Menu\") - 1]"), 30.0f, 1e-4f);
    // Com keyframes (segura no item): valueAtTime em 1..N também.
    Track& mt = ctl->tracks.get_or_create(TP::EffectParam, menu, param_track_key(0, 0));
    (void)mt.set(FrameIndex{0}, 0.0f, Interpolation::Hold);
    (void)mt.set(FrameIndex{30}, 1.0f, Interpolation::Hold);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.layer(\"Ctl\").effect(\"Dropdown Menu Control\")(\"Menu\").valueAtTime(0)"), 1.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.layer(\"Ctl\").effect(\"Dropdown Menu Control\")(\"Menu\").valueAtTime(1.5)"), 2.0f, 1e-4f);
    // Expressão NO menu: o que ela devolve (1..) vira o índice guardado (0..).
    mt.keys.clear();
    mt.expression = expr::compile("3");
    mt.expressionEnabled = true;
    {
        const expr::Scope scope(rig.tl);
        AUREA_CHECK_NEAR(mt.value_or(FrameIndex{0}, 0.0f), 2.0f, 1e-4f);
    }
    mt.expression.reset();

    // Camada: o parâmetro É a camada.
    AUREA_CHECK(!rig.error(probe, TP::PositionX, "thisComp.layer(\"Ctl\").effect(\"Layer Control\")(\"Layer\").position[0]").empty());   // nada escolhido
    pickFx->params[0].constant.ref = targetId.pack();
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.layer(\"Ctl\").effect(\"Layer Control\")(\"Layer\").transform.position[0]"), 300.0f, 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionY, "thisComp.layer(\"Ctl\").effect(\"Layer Control\")(\"Layer\").position"), 400.0f, 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "var L = thisComp.layer(\"Ctl\").effect(\"Layer Control\")(1); L.toWorld([0, 0])[0]"),
                     rig.eval(probe, TP::PositionX, "thisComp.layer(\"Alvo\").toWorld([0, 0])[0]"), 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.layer(\"Ctl\").effect(\"Layer Control\")(\"Layer\").index"),
                     rig.eval(probe, TP::PositionX, "thisComp.layer(\"Alvo\").index"), 1e-4f);
    AUREA_CHECK_EQ(rig.text(probe, "thisComp.layer(\"Ctl\").effect(\"Layer Control\")(\"Layer\").name"), std::string("Alvo"));
    AUREA_CHECK_NEAR(rig.eval(ctl, TP::PositionX, "effect(\"Controle de camada\")(\"Camada\").opacity"), 100.0f, 1e-3f);

    // Ponto 3D: vetor de 3.
    p3Fx->params[0].constant.v[0] = 11.0f;
    p3Fx->params[0].constant.v[1] = 22.0f;
    p3Fx->params[0].constant.v[2] = 33.0f;
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.layer(\"Ctl\").effect(\"3D Point Control\")(\"3D Point\")[2]"), 33.0f, 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.layer(\"Ctl\").effect(\"3D Point Control\")(\"3D Point\").value.length"), 3.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionY, "thisComp.layer(\"Ctl\").effect(\"3D Point Control\")(1)"), 22.0f, 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "length(thisComp.layer(\"Ctl\").effect(\"aurea.control.point3d\")(\"point3d\"), [11, 22, 0])"), 33.0f, 1e-3f);
    Track& zt = ctl->tracks.get_or_create(TP::EffectParam, p3, param_track_key(0, 2));
    (void)zt.set(FrameIndex{0}, 0.0f, Interpolation::Linear);
    (void)zt.set(FrameIndex{30}, 90.0f, Interpolation::Linear);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.layer(\"Ctl\").effect(\"3D Point Control\")(\"3D Point\").valueAtTime(0.5)[2]"), 45.0f, 1e-2f);
    std::printf("Dropdown (1..N, keys, expressao no proprio menu), Layer Control como camada (.transform, .toWorld, .name), 3D Point [x,y,z]; ");
}

// -----------------------------------------------------------------------------
// 4. Caminhos
// -----------------------------------------------------------------------------
AUREA_TEST(ExprAe, PathReadSideForMasksAndShapeGroups) {
    Rig rig;
    LayerId mId, vId;
    Layer* ml = rig.add(LayerKind::Shape, "M", &mId);
    ml->masks.push_back(square_mask(1, "Mask 1", 100.0f));
    Mask round = square_mask(2, "Curva", 60.0f);
    round.closed = false;
    round.points[1].inTangent = Vec2{-10.0f, 0.0f};
    round.points[1].outTangent = Vec2{0.0f, 15.0f};
    ml->masks.push_back(round);
    Layer* vl = rig.add(LayerKind::Shape, "V", &vId);
    vl->shape.shapeType = kShapeVector;
    VectorGroup g;
    g.name = "Grupo";
    VectorPath tri;
    tri.kind = VectorPathKind::Free;
    tri.path.closed = true;
    tri.path.v = {BezierVertex{Vec2{10, 20}, {}, {}}, BezierVertex{Vec2{110, 20}, {}, {}}, BezierVertex{Vec2{110, 70}, {}, {}}};
    g.paths.push_back(tri);
    VectorPath rect;
    rect.kind = VectorPathKind::Rect;
    rect.center = Vec2{0, 0};
    rect.size = Vec2{200, 100};
    g.paths.push_back(rect);
    vl->shape.vector.groups.push_back(g);
    VectorGroup top;
    top.name = "Topo";
    top.paths.push_back(tri);
    vl->shape.vector.groups.push_back(top);
    Layer* probe = rig.add(LayerKind::Null, "Sonda");
    ml = rig.comp->layer(mId);
    vl = rig.comp->layer(vId);
    const TP px = TP::PositionX, py = TP::PositionY;

    // Máscara: pontos, tangentes, fechado.
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(\"Mask 1\").maskPath.points().length"), 4.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(\"Mask 1\").maskPath.points()[2][0]"), 100.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, py, "thisComp.layer(\"M\").mask(1).maskPath.points()[2]"), 100.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(1).maskPath.isClosed() ? 1 : 0"), 1.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(\"Curva\").maskPath.isClosed() ? 1 : 0"), 0.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(2).maskPath.inTangents()[1][0]"), -10.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, py, "thisComp.layer(\"M\").mask(2).maskPath.outTangents()[1]"), 15.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(1).maskPath.inTangents().length"), 4.0f, 1e-4f);
    // Na própria camada: mask() direto; e a forma de grupo do AE (nome da interface ou match name).
    AUREA_CHECK_NEAR(rig.eval(ml, px, "mask(\"Mask 1\").maskPath.points()[1][0]"), 100.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\")(\"Masks\")(\"Mask 1\")(\"Mask Path\").points().length"), 4.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\")(\"ADBE Mask Parade\")(1)(\"ADBE Mask Shape\").points().length"), 4.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\")(\"Transform\")(\"Position\")[0]"), ml->transform.position.x, 1e-3f);
    // pointOnPath anda pelo comprimento: 400 px de perímetro, 12,5% = meio do lado de cima.
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(1).maskPath.pointOnPath(0.125)[0]"), 50.0f, 0.05f);
    AUREA_CHECK_NEAR(rig.eval(probe, py, "thisComp.layer(\"M\").mask(1).maskPath.pointOnPath(0.125)"), 0.0f, 0.05f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(1).maskPath.pointOnPath(0.5)[0]"), 100.0f, 0.05f);
    AUREA_CHECK_NEAR(rig.eval(probe, py, "thisComp.layer(\"M\").mask(1).maskPath.pointOnPath(0.5)"), 100.0f, 0.05f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(1).maskPath.pointOnPath()[0]"), 100.0f, 0.05f);   // padrão: 50%
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(1).maskPath.tangentOnPath(0.125)[0]"), 1.0f, 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, py, "thisComp.layer(\"M\").mask(1).maskPath.tangentOnPath(0.375)"), 1.0f, 1e-3f);  // lado direito: desce
    AUREA_CHECK_NEAR(rig.eval(probe, py, "thisComp.layer(\"M\").mask(1).maskPath.normalOnPath(0.125)"), -1.0f, 1e-3f);  // para fora (cima)
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(1).maskPath.normalOnPath(0.375)[0]"), 1.0f, 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "var p = thisComp.layer(\"M\").mask(1).maskPath; length(p.tangentOnPath(0.3))"), 1.0f, 1e-4f);
    // Caminho aberto: 0 e 1 são as pontas.
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(\"Curva\").maskPath.pointOnPath(0)[0]"), 0.0f, 0.05f);
    AUREA_CHECK_NEAR(rig.eval(probe, py, "thisComp.layer(\"M\").mask(\"Curva\").maskPath.pointOnPath(1)"), 60.0f, 0.05f);
    // Seguir o caminho: posição = ponto do caminho no espaço da composição.
    ml->transform.position = Vec3{500.0f, 300.0f, 0.0f};
    ml->transform.anchor = Vec3{0.0f, 0.0f, 0.0f};
    const char* follow = "var L = thisComp.layer(\"M\"); var p = L.mask(1).maskPath; L.toComp(p.pointOnPath(0.125))";
    AUREA_CHECK_NEAR(rig.eval(probe, px, follow), 550.0f, 0.1f);
    AUREA_CHECK_NEAR(rig.eval(probe, py, follow), 300.0f, 0.1f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::RotationZ, "var t = thisComp.layer(\"M\").mask(1).maskPath.tangentOnPath(0.375); radiansToDegrees(Math.atan2(t[1], t[0]))"), 90.0f, 0.05f);
    // Caminho animado por keys: points(t) lê o instante.
    MaskPathKey k0, k1;
    k0.frame = 0;
    k0.points = ml->masks[0].points;
    k1.frame = 30;
    k1.points = ml->masks[0].points;
    for (MaskPoint& q : k1.points) { q.position.x *= 3.0f; q.position.y *= 3.0f; }
    ml->masks[0].pathKeys = {k0, k1};
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(1).maskPath.points(0)[2][0]"), 100.0f, 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(1).maskPath.points(1)[2][0]"), 300.0f, 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(1).maskPath.points(0.5)[2][0]"), 200.0f, 0.5f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(1).maskPath.points()[2][0]", 30), 300.0f, 1e-3f);
    ml->masks[0].pathKeys.clear();
    // Feather/expansão/opacidade da máscara como propriedade.
    ml->masks[0].feather = 12.0f;
    ml->masks[0].opacity = 0.5f;
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"M\").mask(1).maskFeather + thisComp.layer(\"M\").mask(1).maskOpacity"), 62.0f, 1e-3f);

    // Forma vetorial: content("Grupo").content("Path 1").path
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"V\").content(\"Grupo\").content(\"Path 1\").path.points().length"), 3.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"V\").content(\"Grupo\").content(1).path.points()[1][0]"), 110.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, py, "thisComp.layer(\"V\").content(\"Grupo\").path.points()[2]"), 70.0f, 1e-4f);   // .path = o 1º
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"V\").content(\"Grupo\").path.isClosed() ? 1 : 0"), 1.0f, 1e-4f);
    AUREA_CHECK(rig.eval(probe, px, "thisComp.layer(\"V\").content(\"Grupo\").content(\"Rectangle Path 2\").path.points().length") >= 4.0f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "var p = thisComp.layer(\"V\").content(\"Grupo\").content(2).path; var a = p.pointOnPath(0); var b = p.pointOnPath(0.5); length(a, b) > 100 ? 1 : 0"), 1.0f, 1e-4f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"V\").content(1).path.points().length"), 3.0f, 1e-4f);        // 1 = o de cima ("Topo")
    AUREA_CHECK_EQ(rig.text(probe, "thisComp.layer(\"V\").content(1).name + \"/\" + thisComp.layer(\"V\").content(2).name"), std::string("Topo/Grupo"));
    AUREA_CHECK_NEAR(rig.eval(vl, px, "content(\"Grupo\").content(\"Path 1\").path.pointOnPath(0)[0]"), 10.0f, 0.05f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"V\")(\"Contents\")(\"Grupo\")(\"Contents\")(\"Path 1\")(\"Path\").points().length"), 3.0f, 1e-4f);
    // createPath como valor: dá para ler de volta.
    AUREA_CHECK_NEAR(num("createPath([[0, 0], [50, 0], [50, 50]], [], [], false).points().length"), 3.0, 1e-12);
    AUREA_CHECK_NEAR(num("createPath([[0, 0], [50, 0], [50, 50]], [], [], false).isClosed() ? 1 : 0"), 0.0, 1e-12);
    AUREA_CHECK_NEAR(num("createPath().points().length + (createPath().isClosed() ? 1 : 0)"), 5.0, 1e-12);            // padrão do AE: quadrado de 100 px
    AUREA_CHECK_NEAR(num("createPath([[0, 0], [100, 0]], [[0, 0], [-20, 5]], [[20, 5], [0, 0]], false).inTangents()[1][0]"), -20.0, 1e-12);
    AUREA_CHECK_NEAR(num("createPath([[0, 0], [100, 0], [100, 100], [0, 100]]).pointOnPath(0.25)[0]"), 100.0, 0.05);
    // Erros com mensagem.
    AUREA_CHECK(!rig.error(probe, px, "thisComp.layer(\"M\").mask(\"Nada\").maskPath.points().length").empty());
    AUREA_CHECK(!rig.error(probe, px, "thisComp.layer(\"M\").mask(9).maskPath.points().length").empty());
    AUREA_CHECK(!rig.error(probe, px, "thisComp.layer(\"M\").content(\"Grupo\").path.points().length").empty());      // não é forma vetorial
    AUREA_CHECK(!rig.error(probe, px, "thisComp.layer(\"V\").content(\"Grupo\").content(7).path.points().length").empty());
    AUREA_CHECK(!rig.error(probe, px, "thisComp.layer(\"M\").transform.position.points().length").empty());
    AUREA_CHECK(!ev("createPath([[0, 0], [1, 1]], [[0, 0]])").ok);
    AUREA_CHECK(!ev("createPath([1, 2, 3, 4, 5])").ok);
    AUREA_CHECK(!ev("createPath([[0, 0], [1, 1]])").ok);        // um caminho não é valor de trilha numérica
    std::printf("mask(x).maskPath e content(g).content(n).path: points/inTangents/outTangents/isClosed/pointOnPath/tangentOnPath/normalOnPath, no tempo; createPath; ");
}

AUREA_TEST(ExprAe, MaskPathExpressionWithCreatePath) {
    Rig rig;
    LayerId id;
    Layer* l = rig.add(LayerKind::Shape, "M", &id);
    l->masks.push_back(square_mask(1, "A", 100.0f));
    l->masks.push_back(square_mask(2, "B", 40.0f));
    l = rig.comp->layer(id);
    Mask& a = l->masks[0];
    Mask& b = l->masks[1];
    // Sem expressão: o caminho gravado, e nada muda para quem já usava evaluate_path.
    AUREA_CHECK_EQ(path_of(rig, a, 0.0).size(), static_cast<usize>(4));
    // createPath com os pontos de antes (thisProperty) dobrados.
    a.pathExpression = expr::compile("var p = thisProperty.points();\ncreatePath(p.map(q => q * 2), thisProperty.inTangents(), thisProperty.outTangents(), thisProperty.isClosed())");
    AUREA_CHECK(a.pathExpression->program != nullptr);
    std::vector<MaskPoint> out = path_of(rig, a, 0.0);
    AUREA_CHECK_EQ(out.size(), static_cast<usize>(4));
    if (out.size() == 4) {
        AUREA_CHECK_NEAR(out[1].position.x, 200.0f, 1e-3f);
        AUREA_CHECK_NEAR(out[2].position.y, 200.0f, 1e-3f);
        AUREA_CHECK_NEAR(out[0].position.x, 0.0f, 1e-3f);
    }
    AUREA_CHECK(a.pathExpression->error().ok);
    // O caminho só gravado continua acessível (edição, e a própria expressão lê o de antes).
    std::vector<MaskPoint> raw;
    mask::evaluate_path_keys(a, 0.0, raw);
    AUREA_CHECK(raw.size() == 4 && raw[1].position.x == 100.0f);
    // No tempo: um triângulo que abre com `time`.
    a.pathExpression = expr::compile("createPath([[0, 0], [time * 100, 0], [0, 100]], [], [[0, 0], [0, 10], [0, 0]], true)");
    out = path_of(rig, a, 30.0);
    AUREA_CHECK_EQ(out.size(), static_cast<usize>(3));
    if (out.size() == 3) {
        AUREA_CHECK_NEAR(out[1].position.x, 100.0f, 1e-3f);
        AUREA_CHECK_NEAR(out[1].outTangent.y, 10.0f, 1e-3f);
    }
    out = path_of(rig, a, 15.0);
    AUREA_CHECK(out.size() == 3 && std::fabs(out[1].position.x - 50.0f) < 1e-3f);
    // O bloco da GPU (a chave do cache) segue a expressão: muda de um quadro para o outro.
    {
        const expr::Scope scope(rig.tl);
        std::vector<Vec4> block0, block1;
        f32 start = 0;
        u64 key0 = 0, key1 = 0;
        AUREA_CHECK(mask::build_block(*l, 15.0, Vec2{0, 0}, 0.2f, block0, start, key0) >= 1u);
        AUREA_CHECK(mask::build_block(*l, 30.0, Vec2{0, 0}, 0.2f, block1, start, key1) >= 1u);
        AUREA_CHECK(key0 != key1);
    }
    // Ler outra máscara: B copia A (com a expressão de A aplicada).
    b.pathExpression = expr::compile("mask(\"A\").maskPath");
    out = path_of(rig, b, 30.0);
    AUREA_CHECK(out.size() == 3 && std::fabs(out[1].position.x - 100.0f) < 1e-3f);
    // Dirigida por um controle da camada.
    a.pathExpression = expr::compile("var s = transform.opacity; createPath([[0, 0], [s, 0], [s, s], [0, s]])");
    out = path_of(rig, a, 0.0);
    AUREA_CHECK(out.size() == 4 && std::fabs(out[2].position.x - 100.0f) < 1e-3f);
    // Erro (resultado que não é caminho, sintaxe, execução): vale o caminho gravado e o erro fica na expressão.
    a.pathExpression = expr::compile("42");
    out = path_of(rig, a, 0.0);
    AUREA_CHECK(out.size() == 4 && out[1].position.x == 100.0f);
    AUREA_CHECK(!a.pathExpression->error().ok && a.pathExpression->error().message.find("caminho") != std::string::npos);
    a.pathExpression = expr::compile("createPath([[0, 0], [1 / 0, 5]])");
    out = path_of(rig, a, 0.0);
    AUREA_CHECK(out.size() == 4 && out[1].position.x == 100.0f);
    AUREA_CHECK(!a.pathExpression->error().ok);
    a.pathExpression = expr::compile("createPath([[0, 0], ");
    AUREA_CHECK(a.pathExpression->program == nullptr);
    out = path_of(rig, a, 0.0);
    AUREA_CHECK(out.size() == 4 && out[1].position.x == 100.0f);
    // Desligada: o gravado.
    a.pathExpression = expr::compile("createPath([[0, 0], [9, 0], [9, 9]])");
    a.pathExpressionEnabled = false;
    AUREA_CHECK_EQ(path_of(rig, a, 0.0).size(), static_cast<usize>(4));
    a.pathExpressionEnabled = true;
    AUREA_CHECK_EQ(path_of(rig, a, 0.0).size(), static_cast<usize>(3));
    // Ciclo A <-> B: nenhuma das duas trava; cada uma fica com o caminho gravado.
    a.pathExpression = expr::compile("mask(\"B\").maskPath");
    b.pathExpression = expr::compile("mask(\"A\").maskPath");
    out = path_of(rig, a, 0.0);
    AUREA_CHECK(out.size() == 4);
    out = path_of(rig, b, 0.0);
    AUREA_CHECK(out.size() == 4);
    // Laço sem fim na expressão do caminho: cortado pelo limite.
    a.pathExpression = expr::compile("while (true) { } createPath()");
    b.pathExpression.reset();
    out = path_of(rig, a, 0.0);
    AUREA_CHECK(out.size() == 4 && !a.pathExpression->error().ok);
    // Fora de um escopo de quadro, sem motor registrado: o gravado (sem travar nem estourar).
    a.pathExpression = expr::compile("createPath([[0, 0], [9, 0], [9, 9]])");
    std::vector<MaskPoint> loose;
    mask::evaluate_path(a, 0.0, loose);
    AUREA_CHECK(loose.size() == 4 || loose.size() == 3);
    std::printf("expressao no caminho da mascara: createPath no tempo, thisProperty = caminho de antes, outra mascara, erro/ciclo/limite voltam ao gravado; ");
}

// -----------------------------------------------------------------------------
// 5. sampleImage
// -----------------------------------------------------------------------------
AUREA_TEST(ExprAe, SampleImageReadsThePublishedLayerImage) {
    Rig rig;
    LayerId imgId, solidId, nullId;
    (void)rig.add(LayerKind::Image, "Img", &imgId);
    Layer* solid = rig.add(LayerKind::Shape, "Solido", &solidId);
    solid->shape.shapeType = 0;
    solid->shape.bounds = Rect{0, 0, 200, 100};
    solid->shape.fillColor = Vec4{0.25f, 0.5f, 0.75f, 1.0f};
    (void)rig.add(LayerKind::Video, "Sem", &nullId);
    Layer* probe = rig.add(LayerKind::Null, "Sonda");
    expr::clear_layer_images(&rig.tl);
    std::vector<expr::ImageRequest> requests;
    (void)expr::take_image_requests(requests);
    requests.clear();

    // Sem imagem publicada: zeros e um pedido na fila (uma vez só por camada).
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "thisComp.layer(\"Img\").sampleImage([50, 50])[3] + 5"), 5.0f, 1e-5f);
    (void)rig.eval(probe, TP::PositionX, "thisComp.layer(\"Img\").sampleImage([10, 10])[0]");
    AUREA_CHECK_EQ(expr::take_image_requests(requests), 1u);
    AUREA_CHECK(requests.size() == 1 && requests[0].layer == imgId.pack() && requests[0].comp == 1u && requests[0].postEffect);
    AUREA_CHECK_EQ(expr::take_image_requests(requests), 0u);

    // Publicada: 2×2 cobrindo 200×200 px da camada (vermelho, verde / azul, branco meio transparente).
    expr::LayerPixels px;
    px.width = 2;
    px.height = 2;
    px.layerWidth = 200.0f;
    px.layerHeight = 200.0f;
    px.rgba = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128};
    expr::publish_layer_image(&rig.tl, 1, imgId.pack(), true, px);
    const char* red = "thisComp.layer(\"Img\").sampleImage([50, 50], [1, 1])";
    auto comp4 = [&](const char* src, f32 out[4]) {
        const std::string s(src);
        out[0] = rig.eval(probe, TP::PositionX, (s + "[0]").c_str());
        out[1] = rig.eval(probe, TP::PositionX, (s + "[1]").c_str());
        out[2] = rig.eval(probe, TP::PositionX, (s + "[2]").c_str());
        out[3] = rig.eval(probe, TP::PositionX, (s + "[3]").c_str());
    };
    f32 c[4];
    comp4(red, c);
    AUREA_CHECK(c[0] == 1.0f && c[1] == 0.0f && c[2] == 0.0f && c[3] == 1.0f);
    comp4("thisComp.layer(\"Img\").sampleImage([150, 50])", c);                 // raio padrão 0,5
    AUREA_CHECK(c[0] == 0.0f && c[1] == 1.0f && c[3] == 1.0f);
    comp4("thisComp.layer(\"Img\").sampleImage([150, 150], [10, 10], true, 0)", c);
    AUREA_CHECK_NEAR(c[0], 1.0f, 1e-5f);
    AUREA_CHECK_NEAR(c[3], 128.0f / 255.0f, 1e-5f);
    // A caixa inteira: média ponderada pelo alfa (cor) e alfa médio.
    comp4("thisComp.layer(\"Img\").sampleImage([100, 100], [100, 100])", c);
    const f32 wa = 128.0f / 255.0f, total = 3.0f + wa;
    AUREA_CHECK_NEAR(c[0], (1.0f + wa) / total, 1e-4f);
    AUREA_CHECK_NEAR(c[1], (1.0f + wa) / total, 1e-4f);
    AUREA_CHECK_NEAR(c[2], (1.0f + wa) / total, 1e-4f);
    AUREA_CHECK_NEAR(c[3], total / 4.0f, 1e-4f);
    // Metade da caixa fora da camada: fora conta como transparente.
    comp4("thisComp.layer(\"Img\").sampleImage([0, 50], [50, 50])", c);
    AUREA_CHECK_NEAR(c[0], 1.0f, 1e-5f);
    AUREA_CHECK_NEAR(c[3], 0.5f, 1e-4f);
    comp4("thisComp.layer(\"Img\").sampleImage([-500, -500], [5, 5])", c);
    AUREA_CHECK(c[3] == 0.0f);
    // Na própria camada, sem "thisLayer.".
    Layer* img = rig.comp->layer(imgId);
    AUREA_CHECK_NEAR(rig.eval(img, TP::Opacity, "sampleImage([50, 150])[2] * 100"), 1.0f, 1e-5f);   // azul = 1 → 100 % → guardado 1,0
    // O uso típico: a cor do pixel dirige uma propriedade.
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, "var c = thisComp.layer(\"Img\").sampleImage([50, 50]); rgbToHsl(c)[2] * 200"), 100.0f, 1e-2f);
    AUREA_CHECK_EQ(expr::take_image_requests(requests), 0u);                      // com imagem não pede de novo
    // Fonte síncrona de CPU registrada (pixels da fonte, sem efeitos).
    struct Src { int calls = 0; std::shared_ptr<const expr::LayerPixels> image; } src;
    {
        auto small = std::make_shared<expr::LayerPixels>();
        small->width = small->height = 1;
        small->layerWidth = 100.0f;
        small->layerHeight = 100.0f;
        small->rgba = {51, 102, 153, 255};
        src.image = small;
    }
    expr::register_image_source([](void* ctx, const Timeline*, const Layer& layer, f64) -> std::shared_ptr<const expr::LayerPixels> {
        Src* s = static_cast<Src*>(ctx);
        ++s->calls;
        return layer.name == "Sem" ? s->image : nullptr;
    }, &src);
    comp4("thisComp.layer(\"Sem\").sampleImage([10, 10], [2, 2], false)", c);
    AUREA_CHECK_NEAR(c[0], 0.2f, 1e-5f);
    AUREA_CHECK_NEAR(c[1], 0.4f, 1e-5f);
    AUREA_CHECK_NEAR(c[2], 0.6f, 1e-5f);
    AUREA_CHECK(src.calls >= 4);                                                  // a fonte guarda a imagem pronta; a leitura não copia
    expr::unregister_image_source(&src);
    // Forma/sólido sem imagem: a cor chapada que o modelo conhece, dentro da caixa.
    comp4("thisComp.layer(\"Solido\").sampleImage([20, 20])", c);
    AUREA_CHECK(c[0] == 0.25f && c[1] == 0.5f && c[2] == 0.75f && c[3] == 1.0f);
    comp4("thisComp.layer(\"Solido\").sampleImage([20, 500])", c);
    AUREA_CHECK(c[3] == 0.0f);
    AUREA_CHECK(!rig.error(probe, TP::PositionX, "thisComp.layer(\"Img\").sampleImage(5)[0]").empty());
    AUREA_CHECK(!rig.error(probe, TP::PositionX, "thisComp.layer(\"Img\").sampleImage([1, 1], \"x\")[0]").empty());
    expr::clear_layer_images(&rig.tl);
    comp4(red, c);
    AUREA_CHECK(c[3] == 0.0f);                                                    // limpo: volta a zeros
    (void)expr::take_image_requests(requests);
    std::printf("sampleImage pela imagem publicada (media ponderada pelo alfa, fora = transparente), fonte de CPU, cor chapada, fila de pedidos; ");
}

// -----------------------------------------------------------------------------
// 7. Leitura no tempo de parâmetros de efeito de OUTRA camada; sourceRectAtTime
// -----------------------------------------------------------------------------
AUREA_TEST(ExprAe, ValueAtTimeAndKeysOnOtherLayersEffectParams) {
    Rig rig;
    LayerId ctlId;
    Layer* ctl = rig.add(LayerKind::Null, "Ctl", &ctlId);
    const u32 slider = add_control(*ctl, effect_keys::kSliderControl);
    const u32 point = add_control(*ctl, effect_keys::kPointControl);
    const u32 color = add_control(*ctl, effect_keys::kColorControl);
    Layer* probe = rig.add(LayerKind::Null, "Sonda");
    ctl = rig.comp->layer(ctlId);
    ctl->start = FrameIndex{0};
    Track& st = ctl->tracks.get_or_create(TP::EffectParam, slider, param_track_key(0, 0));
    (void)st.set(FrameIndex{0}, 0.0f, Interpolation::Linear);
    (void)st.set(FrameIndex{30}, 100.0f, Interpolation::Linear);
    (void)st.set(FrameIndex{60}, 40.0f, Interpolation::Linear);
    Track& pxt = ctl->tracks.get_or_create(TP::EffectParam, point, param_track_key(0, 0));
    Track& pyt = ctl->tracks.get_or_create(TP::EffectParam, point, param_track_key(0, 1));
    (void)pxt.set(FrameIndex{0}, 10.0f, Interpolation::Linear);
    (void)pxt.set(FrameIndex{30}, 110.0f, Interpolation::Linear);
    (void)pyt.set(FrameIndex{0}, 5.0f, Interpolation::Linear);
    (void)pyt.set(FrameIndex{30}, 25.0f, Interpolation::Linear);
    const TP px = TP::PositionX, py = TP::PositionY;
    const char* S = "thisComp.layer(\"Ctl\").effect(\"Slider Control\")(\"Slider\")";
    auto at = [&](const char* tail, TP p = TP::PositionX, i64 frame = 0) { return rig.eval(probe, p, (std::string(S) + tail).c_str(), frame); };
    AUREA_CHECK_NEAR(at(".valueAtTime(0.5)"), 50.0f, 1e-2f);
    AUREA_CHECK_NEAR(at(".valueAtTime(1.5)"), 70.0f, 1e-2f);
    AUREA_CHECK_NEAR(at(".valueAtTime(time - 0.5)", px, 30), 50.0f, 1e-2f);       // atraso: o clássico
    AUREA_CHECK_NEAR(at(".numKeys"), 3.0f, 1e-4f);
    AUREA_CHECK_NEAR(at(".key(2).value"), 100.0f, 1e-3f);
    AUREA_CHECK_NEAR(at(".key(2).time"), 1.0f, 1e-5f);
    AUREA_CHECK_NEAR(at(".key(3).index + " "thisComp.layer(\"Ctl\").effect(\"Slider Control\")(\"Slider\").key(1).value"), 3.0f, 1e-4f);
    AUREA_CHECK_NEAR(at(".nearestKey(0.9).index"), 2.0f, 1e-4f);
    AUREA_CHECK_NEAR(at(".nearestKey(1.9).value"), 40.0f, 1e-3f);
    AUREA_CHECK_NEAR(at(".velocityAtTime(0.5)"), 100.0f, 0.5f);
    AUREA_CHECK_NEAR(at(".velocityAtTime(1.5)"), -60.0f, 0.5f);
    AUREA_CHECK_NEAR(at(".speedAtTime(1.5)"), 60.0f, 0.5f);
    AUREA_CHECK_NEAR(at(".value", px, 15), 50.0f, 1e-2f);
    // Camada do controle deslocada no tempo: os tempos são os da COMPOSIÇÃO.
    ctl->start = FrameIndex{30};
    AUREA_CHECK_NEAR(at(".key(2).time"), 2.0f, 1e-5f);
    AUREA_CHECK_NEAR(at(".valueAtTime(1.5)"), 50.0f, 1e-2f);
    ctl->start = FrameIndex{0};
    // Parâmetro vetorial (ponto) e cor.
    const char* P = "thisComp.layer(\"Ctl\").effect(\"Point Control\")(\"Point\")";
    AUREA_CHECK_NEAR(rig.eval(probe, px, (std::string(P) + ".valueAtTime(0.5)").c_str()), 60.0f, 1e-2f);
    AUREA_CHECK_NEAR(rig.eval(probe, py, (std::string(P) + ".valueAtTime(0.5)").c_str()), 15.0f, 1e-2f);
    AUREA_CHECK_NEAR(rig.eval(probe, py, (std::string(P) + ".key(2).value").c_str()), 25.0f, 1e-3f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, (std::string(P) + ".velocityAtTime(0.5)[0]").c_str()), 100.0f, 0.5f);
    ctl->find_effect(EffectId{color, 0})->params[0].constant = ParamValue{};
    ctl->find_effect(EffectId{color, 0})->params[0].constant.v[1] = 0.5f;
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"Ctl\").effect(\"Color Control\")(\"Color\").valueAtTime(3)[1] * 10"), 5.0f, 1e-3f);
    // E um parâmetro de um efeito "de verdade" (não só os controles), por índice.
    AUREA_CHECK(!rig.error(probe, px, "thisComp.layer(\"Ctl\").effect(\"Slider Control\")(\"Slider\").key(9).value").empty());
    std::printf("valueAtTime/velocityAtTime/speedAtTime/key/nearestKey/numKeys em parametro de efeito de outra camada (escalar, ponto, cor), no tempo da composicao; ");
}

AUREA_TEST(ExprAe, SourceRectAtTimeForTextAndVectorLayers) {
    Rig rig;
    LayerId vId, tId;
    Layer* vl = rig.add(LayerKind::Shape, "V", &vId);
    vl->shape.shapeType = kShapeVector;
    VectorGroup g;
    g.name = "Grupo";
    VectorPath tri;
    tri.kind = VectorPathKind::Free;
    tri.path.closed = true;
    tri.path.v = {BezierVertex{Vec2{10, 20}, {}, {}}, BezierVertex{Vec2{110, 20}, {}, {}}, BezierVertex{Vec2{110, 70}, {}, {}}};
    g.paths.push_back(tri);
    g.stroke.enabled = false;
    vl->shape.vector.groups.push_back(g);
    Layer* text = rig.add(LayerKind::Text, "T", &tId);
    text->text.content = "Aurea";
    text->text.size = 60.0f;
    text->text.alignment = 0;
    Layer* probe = rig.add(LayerKind::Null, "Sonda");
    vl = rig.comp->layer(vId);
    text = rig.comp->layer(tId);
    const TP px = TP::PositionX;
    // Vetor: a caixa do conteúdo (de outra camada e da própria).
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"V\").sourceRectAtTime().width"), 100.0f, 0.5f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"V\").sourceRectAtTime().height"), 50.0f, 0.5f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"V\").sourceRectAtTime().left"), 10.0f, 0.5f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"V\").sourceRectAtTime().top"), 20.0f, 0.5f);
    AUREA_CHECK_NEAR(rig.eval(vl, px, "var r = thisLayer.sourceRectAtTime(); r.left + r.width / 2"), 60.0f, 0.5f);
    AUREA_CHECK_NEAR(rig.eval(vl, px, "var r = sourceRectAtTime(time); r.top + r.height"), 70.0f, 0.5f);
    // Vetor com morph: a caixa no instante pedido.
    VectorPath& p = vl->shape.vector.groups[0].paths[0];
    PathKey k0, k1;
    k0.frame = 0;
    k0.path = p.path;
    k0.ease = 0;
    k1.frame = 30;
    k1.path = p.path;
    k1.ease = 0;
    for (BezierVertex& bv : k1.path.v) bv.p.x *= 2.0f;
    p.keys = {k0, k1};
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"V\").sourceRectAtTime(0).width"), 100.0f, 0.5f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"V\").sourceRectAtTime(1).width"), 200.0f, 0.5f);
    AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"V\").sourceRectAtTime(0.5).width"), 150.0f, 1.0f);
    // Texto: a mesma caixa do layout, de outra camada e da própria.
    if (const auto font = text::FontManager::instance().font_for(text->text)) {
        const text::TextExtent ext = text::measure(*font, text->text);
        AUREA_CHECK(ext.width > 10.0f && ext.height > 10.0f);
        AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"T\").sourceRectAtTime().width"), ext.width, 0.01f);
        AUREA_CHECK_NEAR(rig.eval(probe, px, "thisComp.layer(\"T\").sourceRectAtTime().height"), ext.height, 0.01f);
        AUREA_CHECK_NEAR(rig.eval(text, px, "thisLayer.sourceRectAtTime().width"), ext.width, 0.01f);
        AUREA_CHECK_NEAR(rig.eval(text, px, "var r = sourceRectAtTime(time, false); r.width"), ext.width, 0.01f);
        // A caixa de fundo que acompanha o texto (o uso clássico), numa forma vetorial.
        AUREA_CHECK_NEAR(rig.eval(vl, px, "var r = thisComp.layer(\"T\").sourceRectAtTime(); r.width + 40"), ext.width + 40.0f, 0.01f);
    } else {
        std::printf("(texto: sem fonte no host) ");
    }
    std::printf("sourceRectAtTime em camada vetorial (com morph no tempo) e de texto, da propria camada e de outra; ");
}

// -----------------------------------------------------------------------------
// Motor: API, desfazer, salvar e reabrir
// -----------------------------------------------------------------------------
AUREA_TEST(ExprAe, EngineApiMenuItemsAndMaskPathExpressionSurviveUndoAndReopen) {
    EngineConfig cfg;
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    Engine e;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    const auto id = e.add_shape(0);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    auto layer = [&]() { return e.project()->timeline().composition(e.project()->timeline().current())->layer(LayerId::unpack(*id)); };
    u32 menu = 0;
    {
        Layer* l = layer();
        menu = add_control(*l, effect_keys::kDropdownControl);
        l->masks.push_back(square_mask(7, "Mask 1", 100.0f));
    }
    const u32 mp = static_cast<u32>(TP::MaskPath);
    // Itens do menu.
    AUREA_CHECK_EQ(e.effect_menu_items(*id, menu).size(), static_cast<usize>(3));
    AUREA_CHECK(e.set_effect_menu_items(*id, menu, {"Baixo", "Medio", "Alto", "Maximo|x"}));
    std::vector<std::string> items = e.effect_menu_items(*id, menu);
    AUREA_CHECK(items.size() == 4 && items[0] == "Baixo" && items[3] == "Maximo x");
    AUREA_CHECK(!e.set_effect_menu_items(*id, 99999, {"a"}));
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(e.effect_menu_items(*id, menu).size(), static_cast<usize>(3));
    AUREA_CHECK(e.set_effect_menu_items(*id, menu, {"Baixo", "Medio", "Alto", "Maximo"}));
    // A linha do parâmetro leva os itens da instância para a interface.
    {
        std::vector<bridge::EffectParamRow> rows(8);
        std::vector<char> blob(2048);
        const u32 n = e.query_effect_params(*id, menu, rows.data(), static_cast<u32>(rows.size()), blob.data(), static_cast<u32>(blob.size()));
        AUREA_CHECK(n >= 1u);
        if (n >= 1) {
            AUREA_CHECK_EQ(rows[0].enumCount, 4u);
            const std::string labels(blob.data() + rows[0].enumOffset, rows[0].enumLength);
            AUREA_CHECK_EQ(labels, std::string("Baixo|Medio|Alto|Maximo"));
        }
    }
    // Expressão no caminho da máscara pela mesma API das outras expressões.
    expr::Diagnostic diag;
    AUREA_CHECK(e.set_expression(*id, mp, 7, 0, "createPath([[0, 0], [300, 0], [300, 200]])", &diag).ok() && diag.ok);
    Engine::ExpressionInfo info;
    AUREA_CHECK(e.query_expression(*id, mp, 7, 0, info) && info.exists && info.enabled);
    AUREA_CHECK(info.error.ok);
    AUREA_CHECK_NEAR(info.value, 3.0f, 1e-4f);                     // pontos do caminho no cabeçote
    AUREA_CHECK_EQ(info.source, std::string("createPath([[0, 0], [300, 0], [300, 200]])"));
    u32 rows[16 * 4]{};
    AUREA_CHECK_EQ(e.query_expressions(*id, rows, 16), 1u);
    AUREA_CHECK(rows[0] == mp && rows[1] == 7u && (rows[3] & 1u) == 1u && (rows[3] & 2u) == 0u);
    AUREA_CHECK(!e.set_expression(*id, mp, 999, 0, "createPath()").ok());   // máscara que não existe
    AUREA_CHECK(e.set_expression_enabled(*id, mp, 7, 0, false));
    AUREA_CHECK(e.query_expression(*id, mp, 7, 0, info) && info.exists && !info.enabled);
    AUREA_CHECK_NEAR(info.value, 4.0f, 1e-4f);                     // desligada: o caminho gravado
    AUREA_CHECK(e.apply_command(undo).ok());                       // desfaz o desligar
    AUREA_CHECK(e.query_expression(*id, mp, 7, 0, info) && info.enabled);
    // Erro de execução aparece no diagnóstico (resultado que não é caminho).
    AUREA_CHECK(e.set_expression(*id, mp, 7, 0, "time * 2", &diag).ok() && diag.ok);
    AUREA_CHECK(e.query_expression(*id, mp, 7, 0, info) && !info.error.ok);
    AUREA_CHECK(e.set_expression(*id, mp, 7, 0, "createPath([[0, 0], [300, 0], [300, 200]])").ok());
    // Salvar e reabrir: itens e expressão (ligada) de volta.
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_expr_ae.aurea";
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    items = e.effect_menu_items(*id, menu);
    AUREA_CHECK(items.size() == 4 && items[1] == "Medio" && items[3] == "Maximo");
    AUREA_CHECK(e.query_expression(*id, mp, 7, 0, info) && info.exists && info.enabled && info.error.ok);
    AUREA_CHECK_NEAR(info.value, 3.0f, 1e-4f);
    if (const Layer* l = layer()) {
        AUREA_CHECK(l->masks.size() == 1 && l->masks[0].pathExpression != nullptr);
        AUREA_CHECK(l->masks[0].points.size() == 4);              // o caminho gravado não foi trocado
    }
    // Remover (fonte vazia) e salvar de novo: o projeto sem nada disso reabre limpo.
    AUREA_CHECK(e.set_expression(*id, mp, 7, 0, "").ok());
    AUREA_CHECK(e.set_effect_menu_items(*id, menu, {}));
    AUREA_CHECK_EQ(e.query_expressions(*id, rows, 16), 0u);
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(e.effect_menu_items(*id, menu).size(), static_cast<usize>(3));
    AUREA_CHECK(e.query_expression(*id, mp, 7, 0, info) && !info.exists);
    std::remove(path.c_str());
    e.shutdown();
    std::printf("API: itens do menu e expressao do caminho com desfazer, linha do parametro, salvar/reabrir (secao Expressions); ");
}

AUREA_TEST(ExprAe, EngineAnswersFootageAndSampleImageForImageLayers) {
    EngineConfig cfg;
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    Engine e;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    // 4×2: metade esquerda vermelha, metade direita azul meio transparente.
    std::vector<u8> px(4 * 2 * 4);
    for (u32 y = 0; y < 2; ++y) {
        for (u32 x = 0; x < 4; ++x) {
            u8* q = &px[(y * 4 + x) * 4];
            q[0] = x < 2 ? 255 : 0; q[1] = 0; q[2] = x < 2 ? 0 : 255; q[3] = x < 2 ? 255 : 128;
        }
    }
    const auto img = e.import_image(px.data(), 4, 2, "foto", nullptr, 1);
    const auto shape = e.add_shape(0);
    AUREA_CHECK(img.ok() && shape.ok());
    if (!img.ok() || !shape.ok()) { e.shutdown(); return; }
    const Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const Layer* il = comp->layer(LayerId::unpack(*img));
    AUREA_CHECK(il && il->kind == LayerKind::Image);
    const std::string L = "thisComp.layer(\"" + (il ? il->name : std::string("foto")) + "\")";
    const u32 rot = static_cast<u32>(TP::RotationZ);
    auto value = [&](const std::string& src) {
        expr::Diagnostic diag;
        AUREA_CHECK(e.set_expression(*shape, rot, kInvalidIndex, 0, src.c_str(), &diag).ok() && diag.ok);
        Engine::ExpressionInfo info;
        AUREA_CHECK(e.query_expression(*shape, rot, kInvalidIndex, 0, info) && info.exists);
        if (!info.error.ok) std::printf("\n    [expr] '%s' falhou: %s\n", src.c_str(), info.error.message.c_str());
        return info.error.ok ? info.value : -12345.0f;
    };
    // sampleImage: os pixels da FONTE da camada de imagem (o motor já os tem na CPU).
    AUREA_CHECK_NEAR(value(L + ".sampleImage([1, 1], [0.5, 0.5])[0] * 100"), 100.0f, 1e-2f);
    AUREA_CHECK_NEAR(value(L + ".sampleImage([1, 1])[2] * 100"), 0.0f, 1e-2f);
    AUREA_CHECK_NEAR(value(L + ".sampleImage([3, 1])[2] * 100"), 100.0f, 1e-2f);
    AUREA_CHECK_NEAR(value(L + ".sampleImage([3, 1])[3] * 100"), 128.0f / 255.0f * 100.0f, 0.5f);
    AUREA_CHECK_NEAR(value(L + ".sampleImage([2, 1], [2, 1])[3] * 100"), (1.0f + 128.0f / 255.0f) * 50.0f, 0.5f);   // a imagem inteira
    AUREA_CHECK_NEAR(value("var c = " + L + ".sampleImage([0.5, 0.5]); rgbToHsl(c)[2] * 100"), 50.0f, 0.1f);
    // footage()/layer.source: os arquivos do projeto.
    AUREA_CHECK_NEAR(value(L + ".source.width * 10 + " + L + ".source.height"), 42.0f, 1e-3f);
    AUREA_CHECK_NEAR(value("footage(" + L + ".source.name).width"), 4.0f, 1e-3f);
    e.shutdown();
    std::printf("motor: sampleImage le a fonte das camadas de imagem (reduzida a <= 128 px, uma vez), footage()/layer.source leem os arquivos do projeto; ");
}

// -----------------------------------------------------------------------------
// 8. Os atalhos do Aurea PC (ExpressionSheet.kt, só com PlatformFeatures.desktopAe)
// -----------------------------------------------------------------------------
AUREA_TEST(ExprAe, DesktopSheetSnippetsEvaluateAsInserted) {
    // AeSnipColor
    auto r = ev("c = hexToRgb(\"FF8800\");\nhsl = rgbToHsl(c);\nhslToRgb([(hsl[0] + time * 0.1) % 1, hsl[1], hsl[2], 1]);", 0.0, {0.0, 0.0, 0.0, 1.0});
    AUREA_CHECK(r.ok && r.count == 4);
    AUREA_CHECK_NEAR(r.v[0], 1.0, 1e-6); AUREA_CHECK_NEAR(r.v[1], 136.0 / 255.0, 1e-6);
    AUREA_CHECK_NEAR(r.v[2], 0.0, 1e-6); AUREA_CHECK_NEAR(r.v[3], 1.0, 1e-9);
    // AeSnipFunction
    r = ev("function pulse(t, freq, amount) {\n  return 1 + Math.sin(t * freq * 2 * Math.PI) * amount;\n}\nvalue * pulse(time, 2, 0.1);", 0.125, {100.0});
    AUREA_CHECK(r.ok);
    AUREA_CHECK_NEAR(r.v[0], 110.0, 1e-6);
    Rig rig;
    LayerId probeId;
    Layer* probe = rig.add(LayerKind::Null, "Sonda", &probeId);
    const u32 menu = add_control(*probe, effect_keys::kDropdownControl);
    LayerId mId;
    Layer* ml = rig.add(LayerKind::Shape, "M", &mId);        // a de cima: layer(1)
    ml->masks.push_back(square_mask(1, "Mask 1", 100.0f));
    ml->transform.position = Vec3{0.0f, 0.0f, 0.0f};
    ml->transform.anchor = Vec3{0.0f, 0.0f, 0.0f};
    probe = rig.comp->layer(probeId);
    // AeSnipPathFollow (posição): meio segundo = 12,5% do caminho = meio do lado de cima.
    const char* follow = "L = thisComp.layer(1);\np = L.mask(1).maskPath;\nt = (time * 0.25) % 1;\nL.toComp(p.pointOnPath(t));";
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionX, follow, 15), 50.0f, 0.1f);
    AUREA_CHECK_NEAR(rig.eval(probe, TP::PositionY, follow, 15), 0.0f, 0.1f);
    // AeSnipDropdown (na opacidade: 40 % com key).
    const char* pick = "m = effect(\"Dropdown Menu Control\")(\"Menu\").value;\nswitch (m) {\n  case 1: value; break;\n  case 2: value * 1.5; break;\n  default: value * 2;\n}";
    (void)probe->tracks.get_or_create(TP::Opacity).set(FrameIndex{0}, 0.4f, Interpolation::Linear);   // value = 40 %
    AUREA_CHECK_NEAR(rig.eval(probe, TP::Opacity, pick), 0.4f, 1e-4f);
    probe->find_effect(EffectId{menu, 0})->params[0].constant.v[0] = 1.0f;
    AUREA_CHECK_NEAR(rig.eval(probe, TP::Opacity, pick), 0.6f, 1e-4f);
    probe->find_effect(EffectId{menu, 0})->params[0].constant.v[0] = 2.0f;
    AUREA_CHECK_NEAR(rig.eval(probe, TP::Opacity, pick), 0.8f, 1e-4f);
    std::printf("atalhos do PC: cor, seguir caminho, trocar pelo menu e funcao propria rodam como inseridos; ");
}
