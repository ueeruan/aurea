// =============================================================================
//  Testes das expressões (expr/Expression.hpp): linguagem, sandbox, funções do
//  After Effects, referências entre camadas, controles, ciclos, fallback,
//  salvar/reabrir e custo por quadro.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/timeline/Timeline.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace aurea;

namespace {

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

struct Rig {
    Timeline tl;
    Composition* comp = nullptr;
    Rig() {
        const CompositionId id = tl.create_composition("C", 1920, 1080, 30.0);
        tl.set_current(id);
        comp = tl.composition(id);
    }
    Layer* add(const char* name, LayerId* outId = nullptr) {
        const LayerId id = comp->add_layer(LayerKind::Shape, name);
        Layer* l = comp->layer(id);
        l->start = FrameIndex{0};
        l->end = FrameIndex{300};
        if (outId) *outId = id;
        return l;
    }
};

/// Um "quadro": escopo novo a cada leitura (o modelo pode ter mudado entre
/// uma leitura e outra — dentro de um escopo ele não muda, é a precondição).
f32 val(Rig& rig, const Track& t, i64 f, f32 fallback) {
    const expr::Scope scope(rig.tl);
    return t.value_or(FrameIndex{f}, fallback);
}

void set_expr(Track& t, const char* src) {
    t.expression = expr::compile(src);
    t.expressionEnabled = true;
}

u32 add_control(Layer& l, const char* key, f32 constant) {
    EffectInstance inst;
    inst.id = l.alloc_effect_id();
    inst.type = effect_type_id(key);
    initialize_instance(inst, *expr::builtin_effects().params(inst.type));
    inst.params[0].constant.v[0] = constant;
    l.effects.push_back(inst);
    return inst.id;
}

} // namespace

// -----------------------------------------------------------------------------
// Linguagem
// -----------------------------------------------------------------------------
AUREA_TEST(Expr, ArithmeticPrecedenceAndStatements) {
    AUREA_CHECK_NEAR(num("1 + 2 * 3"), 7.0, 1e-12);
    AUREA_CHECK_NEAR(num("(1 + 2) * 3"), 9.0, 1e-12);
    AUREA_CHECK_NEAR(num("2 ^ 3 ^ 2"), 512.0, 1e-9);      // potência associa à direita
    AUREA_CHECK_NEAR(num("-2 ^ 2"), -4.0, 1e-12);          // -(2^2)
    AUREA_CHECK_NEAR(num("2 ** 10"), 1024.0, 1e-9);
    AUREA_CHECK_NEAR(num("10 % 3"), 1.0, 1e-12);
    AUREA_CHECK_NEAR(num("7 / 2 - 1"), 2.5, 1e-12);
    AUREA_CHECK_NEAR(num("1 < 2 && 3 >= 3"), 1.0, 1e-12);
    AUREA_CHECK_NEAR(num("0 || 5"), 5.0, 1e-12);
    AUREA_CHECK_NEAR(num("!0 + !1"), 1.0, 1e-12);
    AUREA_CHECK_NEAR(num("1 == 1 ? 10 : 20"), 10.0, 1e-12);
    AUREA_CHECK_NEAR(num("var a = 5; let b = a * 2; const c = b + 1; c"), 11.0, 1e-12);
    AUREA_CHECK_NEAR(num("x = 0; for (var i = 0; i < 10; i++) x += i; x"), 45.0, 1e-12);
    AUREA_CHECK_NEAR(num("n = 0; while (n < 7) { n++; if (n == 3) continue; } n"), 7.0, 1e-12);
    AUREA_CHECK_NEAR(num("s = 0; for (i = 0; i < 100; i++) { if (i == 4) break; s += 1 } s"), 4.0, 1e-12);
    AUREA_CHECK_NEAR(num("if (time > 1) 5 else 7", 2.0), 5.0, 1e-12);
    AUREA_CHECK_NEAR(num("if (time > 1) { 5 } else { 7 }", 0.5), 7.0, 1e-12);
    AUREA_CHECK_NEAR(num("return 3; 4"), 3.0, 1e-12);
    AUREA_CHECK_NEAR(num("// comentário\n/* bloco */ 42"), 42.0, 1e-12);
    AUREA_CHECK_NEAR(num("a = 1\nb = 2\na + b"), 3.0, 1e-12);   // sem ';'
    AUREA_CHECK_NEAR(num("time * 90", 2.0), 180.0, 1e-12);
    AUREA_CHECK_NEAR(num("frame", 1.0), 30.0, 1e-12);
    AUREA_CHECK_NEAR(num("Math.PI"), 3.14159265358979, 1e-12);
    AUREA_CHECK_NEAR(num("Math.max(1, 5, 3) + min(4, 2)"), 7.0, 1e-12);
    AUREA_CHECK_NEAR(num("Math.abs(-3) + sqrt(16) + floor(2.7) + ceil(2.1) + round(2.5)"), 3 + 4 + 2 + 3 + 3, 1e-12);
    AUREA_CHECK_NEAR(num("degreesToRadians(180)"), 3.14159265358979, 1e-12);
    AUREA_CHECK_NEAR(num("radiansToDegrees(Math.PI / 2)"), 90.0, 1e-9);
    AUREA_CHECK_NEAR(num("atan2(1, 1) * 4"), 3.14159265358979, 1e-12);
    AUREA_CHECK_NEAR(num("pow(2, 8) + exp(0) + log(1)"), 257.0, 1e-12);
    AUREA_CHECK_NEAR(num("clamp(150, 0, 100)"), 100.0, 1e-12);
    AUREA_CHECK_NEAR(num("framesToTime(45)"), 1.5, 1e-12);
    AUREA_CHECK_NEAR(num("timeToFrames(2)"), 60.0, 1e-12);
    std::printf("aritmetica, precedencia, var/let/const, if/else, for/while/break/continue; ");
}

AUREA_TEST(Expr, VectorsAreComponentWise) {
    auto r = ev("[1, 2] + [10, 20]");
    AUREA_CHECK(r.ok && r.count == 2);
    AUREA_CHECK_NEAR(r.v[0], 11.0, 1e-12); AUREA_CHECK_NEAR(r.v[1], 22.0, 1e-12);
    r = ev("[1, 2, 3] + [10, 0]");                 // o menor completa com zero
    AUREA_CHECK(r.ok && r.count == 3);
    AUREA_CHECK_NEAR(r.v[0], 11.0, 1e-12); AUREA_CHECK_NEAR(r.v[2], 3.0, 1e-12);
    r = ev("[2, 4] * 3 - [1, 1]");
    AUREA_CHECK(r.ok && r.count == 2 && r.v[0] == 5.0 && r.v[1] == 11.0);
    r = ev("[2, 4] / 2");
    AUREA_CHECK(r.ok && r.v[0] == 1.0 && r.v[1] == 2.0);
    r = ev("-[1, -2]");
    AUREA_CHECK(r.ok && r.v[0] == -1.0 && r.v[1] == 2.0);
    AUREA_CHECK_NEAR(num("v = [3, 4]; v[1] * 10 + v[0]"), 43.0, 1e-12);
    AUREA_CHECK_NEAR(num("length([3, 4])"), 5.0, 1e-12);
    AUREA_CHECK_NEAR(num("length([1, 1], [4, 5])"), 5.0, 1e-12);
    AUREA_CHECK_NEAR(num("dot([1, 2, 3], [4, 5, 6])"), 32.0, 1e-12);
    AUREA_CHECK_NEAR(num("normalize([3, 4])[1]"), 0.8, 1e-12);
    AUREA_CHECK_NEAR(num("cross([1, 0, 0], [0, 1, 0])[2]"), 1.0, 1e-12);
    AUREA_CHECK_NEAR(num("add([1, 2], [3, 4])[1] + mul([1, 2], 3)[1]"), 12.0, 1e-12);
    AUREA_CHECK_NEAR(num("[1, 2] == [1, 2]"), 1.0, 1e-12);
    AUREA_CHECK_NEAR(num("clamp([150, -5], 0, 100)[0] + clamp([150, -5], 0, 100)[1]"), 100.0, 1e-12);
    // `value` com dois componentes (posição).
    r = ev("value + [10, 0]", 0.0, {100.0, 200.0});
    AUREA_CHECK(r.ok && r.count == 2 && r.v[0] == 110.0 && r.v[1] == 200.0);
    r = ev("[value[0], value[1] * 2]", 0.0, {100.0, 200.0});
    AUREA_CHECK(r.ok && r.v[1] == 400.0);
    std::printf("vetores: soma com zero-padding, escalar espalha, indice, length/dot/cross/normalize; ");
}

AUREA_TEST(Expr, LinearAndEase) {
    AUREA_CHECK_NEAR(num("linear(0.5, 0, 1, 0, 100)"), 50.0, 1e-12);
    AUREA_CHECK_NEAR(num("linear(2, 0, 1, 0, 100)"), 100.0, 1e-12);       // preso no fim
    AUREA_CHECK_NEAR(num("linear(-1, 0, 1, 0, 100)"), 0.0, 1e-12);        // preso no início
    AUREA_CHECK_NEAR(num("linear(0.25, 10, 20)"), 12.5, 1e-12);           // forma curta t ∈ [0,1]
    AUREA_CHECK_NEAR(num("ease(0.25, 0, 1, 0, 1)"), 0.15625, 1e-12);      // 3u² − 2u³
    AUREA_CHECK_NEAR(num("ease(0.5, 0, 1, 0, 1)"), 0.5, 1e-12);
    AUREA_CHECK_NEAR(num("easeIn(0.5, 0, 1, 0, 1)"), 0.25, 1e-12);
    AUREA_CHECK_NEAR(num("easeOut(0.5, 0, 1, 0, 1)"), 0.75, 1e-12);
    AUREA_CHECK_NEAR(num("linear(time, 0, 2, 0, 360)", 1.0), 180.0, 1e-12);
    const auto r = ev("linear(0.5, 0, 1, [0, 0], [100, 200])");
    AUREA_CHECK(r.ok && r.count == 2);
    AUREA_CHECK_NEAR(r.v[1], 100.0, 1e-12);
    std::printf("linear/ease/easeIn/easeOut (escalar e vetor, presos nas pontas); ");
}

AUREA_TEST(Expr, SyntaxErrorsCarryLineAndColumn) {
    auto r = ev("1 +");
    AUREA_CHECK(!r.ok && r.diag.line == 1);
    r = ev("x = 1;\nfoo(2)");
    AUREA_CHECK(!r.ok);
    AUREA_CHECK(r.diag.line == 2 && r.diag.column == 1);
    AUREA_CHECK(r.diag.message.find("foo") != std::string::npos);
    r = ev("\n\n  2 +* 3");
    AUREA_CHECK(!r.ok && r.diag.line == 3 && r.diag.column == 6);
    r = ev("var = 3");
    AUREA_CHECK(!r.ok);
    r = ev("time = 3");                                   // não dá para atribuir a global
    AUREA_CHECK(!r.ok && r.diag.message.find("time") != std::string::npos);
    r = ev("[1, 2, 3, 4, 5]");
    AUREA_CHECK(!r.ok);
    r = ev("function f() { return 1 }");
    AUREA_CHECK(!r.ok && r.diag.message.find("function") != std::string::npos);
    r = ev("\"abc");
    AUREA_CHECK(!r.ok);
    r = ev("y + 1");                                       // variável sem valor = erro de execução
    AUREA_CHECK(!r.ok);
    r = ev("[1, 2][5]");
    AUREA_CHECK(!r.ok && r.diag.message.find("fora") != std::string::npos);
    r = ev("1 / 0");
    AUREA_CHECK(!r.ok);                                    // resultado não finito
    std::printf("erros com linha/coluna(ex.: \"%s\") ", ev("\n\n  2 +* 3").diag.message.c_str());
}

AUREA_TEST(Expr, SandboxLimitsNeverCrash) {
    auto t0 = std::chrono::steady_clock::now();
    auto r = ev("while (true) {}");
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    AUREA_CHECK(!r.ok && r.diag.message.find("limite") != std::string::npos);
    r = ev("x = 0; for (;;) x++");
    AUREA_CHECK(!r.ok && r.diag.message.find("limite") != std::string::npos);
    // Aninhamento absurdo: erro de sintaxe, não estouro de pilha.
    std::string deep(5000, '(');
    deep += "1";
    deep += std::string(5000, ')');
    r = ev(deep.c_str());
    AUREA_CHECK(!r.ok && r.diag.message.find("profundo") != std::string::npos);
    std::string minus(20000, '-');
    minus += "1";
    r = ev(minus.c_str());
    AUREA_CHECK(!r.ok);
    std::string big(2000, 'a');
    r = ev(("\"" + big + "\"").c_str());
    AUREA_CHECK(!r.ok && r.diag.message.find("longo") != std::string::npos);
    std::string huge(expr::kMaxSourceBytes + 10, ' ');
    r = ev((huge + "1").c_str());
    AUREA_CHECK(!r.ok);
    std::printf("laco infinito cortado em %u instrucoes (%.2f ms); aninhamento/tamanho recusados sem travar ", expr::kMaxInstructions, ms);
}

// -----------------------------------------------------------------------------
// Na timeline
// -----------------------------------------------------------------------------
AUREA_TEST(Expr, WiggleIsDeterministicAndBounded) {
    Rig rig;
    LayerId aid;
    Layer* a = rig.add("A", &aid);
    a->transform.position = Vec3{500.0f, 300.0f, 0.0f};
    Track& tx = a->tracks.get_or_create(TrackProperty::PositionX);
    Track& ty = a->tracks.get_or_create(TrackProperty::PositionY);
    set_expr(tx, "wiggle(2, 30)");
    set_expr(ty, "wiggle(2, 30)");
    f64 minX = 1e9, maxX = -1e9, sum = 0, sum2 = 0;
    u32 differ = 0;
    constexpr u32 kFrames = 3000;
    {
        const expr::Scope scope(rig.tl);
        for (u32 f = 0; f < kFrames; ++f) {
            const f32 x = tx.value_or(FrameIndex{f}, 500.0f);
            const f32 y = ty.value_or(FrameIndex{f}, 300.0f);
            minX = std::min<f64>(minX, x);
            maxX = std::max<f64>(maxX, x);
            sum += x - 500.0;
            sum2 += (x - 500.0) * (x - 500.0);
            if (std::fabs((x - 500.0f) - (y - 300.0f)) > 1e-3f) ++differ;   // X e Y independentes
        }
    }
    AUREA_CHECK(minX >= 470.0 - 1e-3 && maxX <= 530.0 + 1e-3);             // |desvio| ≤ amplitude
    const f64 sd = std::sqrt(sum2 / kFrames - (sum / kFrames) * (sum / kFrames));
    AUREA_CHECK(sd > 4.0);                                                   // mexe de verdade
    AUREA_CHECK(maxX - minX > 30.0);
    AUREA_CHECK(differ > kFrames * 9 / 10);
    // Determinístico: fora do escopo (outro caminho), mesmo resultado.
    Timeline* tl = &rig.tl;
    expr::register_provider([](void* c) -> const Timeline* { return static_cast<Timeline*>(c); }, tl);
    const f32 again = tx.value_or(FrameIndex{1234}, 500.0f);
    f32 inScope = 0;
    {
        const expr::Scope scope(rig.tl);
        inScope = tx.value_or(FrameIndex{1234}, 500.0f);
    }
    expr::unregister_provider(tl);
    AUREA_CHECK_EQ(again, inScope);
    // Oitavas: continua dentro da amplitude.
    set_expr(tx, "wiggle(5, 50, 4, 0.7)");
    f64 m = 0;
    {
        const expr::Scope scope(rig.tl);
        for (u32 f = 0; f < kFrames; ++f) m = std::max<f64>(m, std::fabs(tx.value_or(FrameIndex{f}, 500.0f) - 500.0));
    }
    AUREA_CHECK(m <= 50.0 + 1e-3 && m > 15.0);
    std::printf("wiggle(2,30): faixa [%.1f, %.1f] (limite 470..530), desvio padrao %.1f px, deterministico; 4 oitavas max %.1f (limite 50) ",
                minX, maxX, sd, m);
}

AUREA_TEST(Expr, LoopOutAndLoopInOverKeyframes) {
    Rig rig;
    Layer* a = rig.add("A");
    Track& t = a->tracks.get_or_create(TrackProperty::PositionX);
    (void)t.set(FrameIndex{0}, 0.0f);
    (void)t.set(FrameIndex{30}, 100.0f);
    auto at = [&](const char* src, i64 f) {
        set_expr(t, src);
        return val(rig, t, f, 0.0f);
    };
    AUREA_CHECK_NEAR(at("loopOut(\"cycle\")", 15), 50.0, 1e-3);          // dentro: o próprio keyframe
    AUREA_CHECK_NEAR(at("loopOut(\"cycle\")", 40), 100.0 / 3.0, 1e-3);    // 40 → 10
    AUREA_CHECK_NEAR(at("loopOut()", 75), 50.0, 1e-3);                   // 75 → 15 (padrão cycle)
    AUREA_CHECK_NEAR(at("loopOut(\"pingpong\")", 40), 200.0 / 3.0, 1e-3);// volta: 40 → 20
    AUREA_CHECK_NEAR(at("loopOut(\"pingpong\")", 70), 100.0 / 3.0, 1e-3);// ida de novo: 70 → 10
    AUREA_CHECK_NEAR(at("loopOut(\"offset\")", 40), 100.0 + 100.0 / 3.0, 1e-3);
    AUREA_CHECK_NEAR(at("loopOut(\"offset\")", 100), 300.0 + 100.0 / 3.0, 1e-3);
    AUREA_CHECK_NEAR(at("loopOut(\"continue\")", 40), 100.0 + 100.0 / 3.0, 1e-3);
    AUREA_CHECK_NEAR(at("loopIn(\"cycle\")", -10), 200.0 / 3.0, 1e-3);    // −10 ≡ 20
    AUREA_CHECK_NEAR(at("loopIn(\"pingpong\")", -10), 100.0 / 3.0, 1e-3);
    AUREA_CHECK_NEAR(at("loopOutDuration(\"cycle\", 0.5)", 40), 100.0 * 25.0 / 30.0, 1e-3);   // trecho 15..30
    // Três keyframes: loopOut("cycle", 1) repete só o último trecho.
    (void)t.set(FrameIndex{60}, 40.0f);
    AUREA_CHECK_NEAR(at("loopOut(\"cycle\", 1)", 75), 70.0, 1e-3);        // 75 → 45 (entre 100 e 40)
    AUREA_CHECK_NEAR(at("key(2).value + numKeys", 0), 103.0, 1e-3);
    AUREA_CHECK_NEAR(at("key(3).time", 0), 2.0, 1e-6);
    AUREA_CHECK_NEAR(at("valueAtTime(1)", 0), 100.0, 1e-3);
    AUREA_CHECK_NEAR(at("velocity", 15), 100.0, 1e-2);                   // 100 px/s na subida
    std::printf("loopOut cycle/pingpong/offset/continue, loopIn, loopOutDuration, key(i), valueAtTime, velocity; ");
}

AUREA_TEST(Expr, CrossLayerReferenceUsesTheirExpression) {
    Rig rig;
    Layer* a = rig.add("A");
    Layer* b = rig.add("B");
    a->transform.opacity = 0.8f;
    a->transform.position = Vec3{100.0f, 50.0f, 0.0f};
    Track& bo = b->tracks.get_or_create(TrackProperty::Opacity);
    set_expr(bo, "thisComp.layer(\"A\").transform.opacity * 0.5");
    Track& bx = b->tracks.get_or_create(TrackProperty::PositionX);
    set_expr(bx, "layer(\"A\").transform.position[0] + 10");
    AUREA_CHECK_NEAR(val(rig, bo, 0, 1.0f), 0.4f, 1e-5f);        // 80% × 0,5 = 40%
    AUREA_CHECK_NEAR(val(rig, bx, 0, 0.0f), 110.0f, 1e-4f);
    // A ganha expressão própria: B lê o valor COM a expressão de A.
    Track& ao = a->tracks.get_or_create(TrackProperty::Opacity);
    set_expr(ao, "50");
    AUREA_CHECK_NEAR(val(rig, bo, 0, 1.0f), 0.25f, 1e-5f);
    // Por índice (1 = topo) e nome da camada.
    set_expr(bx, "thisComp.layer(2).name == \"A\" ? thisComp.numLayers * 100 : 0");
    AUREA_CHECK_NEAR(val(rig, bx, 2, 0.0f), 200.0f, 1e-4f);
    set_expr(bx, "index * 10 + thisLayer.index");
    AUREA_CHECK_NEAR(val(rig, bx, 3, 0.0f), 11.0f, 1e-4f);
    std::printf("B.opacidade = A.opacidade*0.5 -> 40%%; com expressao em A (50) -> 25%%; indice/nomes; ");
}

AUREA_TEST(Expr, SliderControlDrivesAProperty) {
    Rig rig;
    Layer* ctrl = rig.add("Controle");
    Layer* b = rig.add("B");
    const u32 sid = add_control(*ctrl, effect_keys::kSliderControl, 42.0f);
    (void)add_control(*ctrl, effect_keys::kSliderControl, 7.0f);
    Track& rot = b->tracks.get_or_create(TrackProperty::RotationZ);
    set_expr(rot, "thisComp.layer(\"Controle\").effect(\"Slider Control\")(\"Slider\")");
    AUREA_CHECK_NEAR(val(rig, rot, 0, 0.0f), 42.0f, 1e-5f);
    set_expr(rot, "thisComp.layer(\"Controle\").effect(\"Slider Control 2\")(1)");
    AUREA_CHECK_NEAR(val(rig, rot, 1, 0.0f), 7.0f, 1e-5f);
    set_expr(rot, "thisComp.layer(\"Controle\").effect(\"Controle deslizante\").param(\"Valor\") * 2");
    AUREA_CHECK_NEAR(val(rig, rot, 2, 0.0f), 84.0f, 1e-5f);
    // O controle animado por keyframes move a propriedade.
    Track& st = ctrl->tracks.get_or_create(TrackProperty::EffectParam, sid, param_track_key(0, 0));
    (void)st.set(FrameIndex{0}, 0.0f);
    (void)st.set(FrameIndex{30}, 100.0f);
    set_expr(rot, "thisComp.layer(\"Controle\").effect(\"Slider Control\")(\"Slider\")");
    AUREA_CHECK_NEAR(val(rig, rot, 15, 0.0f), 50.0f, 1e-4f);
    // Controles de cor e ponto: vetores.
    const u32 pid = add_control(*ctrl, effect_keys::kPointControl, 320.0f);
    (void)pid;
    set_expr(rot, "p = thisComp.layer(\"Controle\").effect(\"Point Control\")(\"Point\"); p[0] + p[1]");
    AUREA_CHECK_NEAR(val(rig, rot, 16, 0.0f), 320.0f, 1e-4f);
    (void)add_control(*ctrl, effect_keys::kCheckboxControl, 1.0f);
    set_expr(rot, "thisComp.layer(\"Controle\").effect(\"Checkbox Control\")(\"Checkbox\") ? 90 : 0");
    AUREA_CHECK_NEAR(val(rig, rot, 17, 0.0f), 90.0f, 1e-4f);
    // O controle não desenha nada: sai da cadeia de efeitos.
    const Effect* fx = expr::builtin_effects().find(effect_type_id(effect_keys::kSliderControl));
    AUREA_CHECK(fx && fx->is_identity(EffectEval{}));
    std::printf("Slider Control -> rotacao 42 / keyframes 0..100 -> 50 no meio; Ponto, Caixa, numero de instancia; ");
}

AUREA_TEST(Expr, CycleIsDetectedAndReported) {
    Rig rig;
    Layer* a = rig.add("A");
    Layer* b = rig.add("B");
    a->transform.opacity = 0.3f;
    b->transform.opacity = 0.6f;
    Track& ao = a->tracks.get_or_create(TrackProperty::Opacity);
    Track& bo = b->tracks.get_or_create(TrackProperty::Opacity);
    set_expr(ao, "thisComp.layer(\"B\").transform.opacity");
    set_expr(bo, "thisComp.layer(\"A\").transform.opacity + 1");
    const f32 va = val(rig, ao, 0, 0.3f);
    AUREA_CHECK_NEAR(va, 0.3f, 1e-6f);   // volta ao valor parado
    const expr::Diagnostic ea = ao.expression->error();
    const expr::Diagnostic eb = bo.expression->error();
    AUREA_CHECK(!ea.ok && ea.message.find("circular") != std::string::npos);
    AUREA_CHECK(!eb.ok && eb.message.find("circular") != std::string::npos);
    // Autorreferência ao PRÓPRIO grupo não é ciclo: lê o valor pré-expressão.
    Layer* c = rig.add("C");
    c->transform.position = Vec3{10.0f, 20.0f, 0.0f};
    Track& cx = c->tracks.get_or_create(TrackProperty::PositionX);
    Track& cy = c->tracks.get_or_create(TrackProperty::PositionY);
    set_expr(cx, "transform.position[1] + 1");
    set_expr(cy, "[transform.position[0], transform.position[0] * 3]");
    AUREA_CHECK_NEAR(val(rig, cx, 0, 10.0f), 21.0f, 1e-5f);
    AUREA_CHECK_NEAR(val(rig, cy, 0, 20.0f), 30.0f, 1e-5f);
    AUREA_CHECK(cx.expression->error().ok && cy.expression->error().ok);
    // Consertar o ciclo limpa o erro.
    set_expr(bo, "70");
    AUREA_CHECK_NEAR(val(rig, ao, 5, 0.3f), 0.7f, 1e-6f);
    AUREA_CHECK(ao.expression->error().ok);
    std::printf("ciclo A->B->A: '%s'; autorreferencia ao proprio grupo sem ciclo ", ea.message.c_str());
}

AUREA_TEST(Expr, ErrorsFallBackToKeyframes) {
    Rig rig;
    Layer* a = rig.add("A");
    Track& t = a->tracks.get_or_create(TrackProperty::PositionX);
    (void)t.set(FrameIndex{0}, 0.0f);
    (void)t.set(FrameIndex{10}, 100.0f);
    set_expr(t, "value +* 2");                                  // sintaxe
    AUREA_CHECK_NEAR(val(rig, t, 5, 0.0f), 50.0f, 1e-4f);
    AUREA_CHECK(!t.expression->error().ok && t.expression->error().column == 8);
    set_expr(t, "x = 1;\nthisComp.layer(\"Nada\").transform.position");   // execução
    AUREA_CHECK_NEAR(val(rig, t, 6, 0.0f), 60.0f, 1e-4f);
    const expr::Diagnostic d = t.expression->error();
    AUREA_CHECK(!d.ok && d.line == 2 && d.message.find("Nada") != std::string::npos);
    set_expr(t, "while (1) {}");                                // sandbox
    AUREA_CHECK_NEAR(val(rig, t, 7, 0.0f), 70.0f, 1e-4f);
    AUREA_CHECK(t.expression->error().message.find("limite") != std::string::npos);
    set_expr(t, "loopOut(\"nada\")");
    AUREA_CHECK_NEAR(val(rig, t, 8, 0.0f), 80.0f, 1e-4f);
    // Desligada: keyframes, sem apagar o texto.
    set_expr(t, "value + 1000");
    t.expressionEnabled = false;
    AUREA_CHECK_NEAR(val(rig, t, 9, 0.0f), 90.0f, 1e-4f);
    t.expressionEnabled = true;
    AUREA_CHECK_NEAR(val(rig, t, 9, 0.0f), 1090.0f, 1e-3f);
    AUREA_CHECK(t.expression->error().ok);
    std::printf("sintaxe/execucao/sandbox/tipo invalido -> valor dos keyframes; erro com linha e coluna; ");
}

AUREA_TEST(Expr, RandomIsDeterministicPerLayerAndFrame) {
    Rig rig;
    Layer* a = rig.add("A");
    Layer* b = rig.add("B");
    Track& ta = a->tracks.get_or_create(TrackProperty::RotationZ);
    Track& tb = b->tracks.get_or_create(TrackProperty::RotationZ);
    set_expr(ta, "random(0, 100)");
    set_expr(tb, "random(0, 100)");
    const f32 a0 = val(rig, ta, 0, 0.0f), a1 = val(rig, ta, 1, 0.0f);
    const f32 b0 = val(rig, tb, 0, 0.0f);
    AUREA_CHECK(a0 >= 0.0f && a0 < 100.0f);
    AUREA_CHECK(a0 != a1 && a0 != b0);
    set_expr(ta, "seedRandom(7, true); random(0, 100)");
    const f32 s0 = val(rig, ta, 10, 0.0f), s1 = val(rig, ta, 20, 0.0f);
    AUREA_CHECK_EQ(s0, s1);                                      // sem tempo: fixo
    set_expr(ta, "noise(time)");
    const f32 n = val(rig, ta, 11, 0.0f);
    AUREA_CHECK(n >= -1.0f && n <= 1.0f);
    std::printf("random por camada/quadro, seedRandom(sem tempo), noise em [-1,1]; ");
}

// -----------------------------------------------------------------------------
// Motor: API, desfazer, salvar/reabrir
// -----------------------------------------------------------------------------
AUREA_TEST(Expr, EngineApiUndoSaveAndReopen) {
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
    const u32 px = static_cast<u32>(TrackProperty::PositionX);
    expr::Diagnostic diag;
    AUREA_CHECK(e.set_expression(*id, px, kInvalidIndex, 0, "value + 100", &diag).ok() && diag.ok);
    Engine::ExpressionInfo info;
    AUREA_CHECK(e.query_expression(*id, px, kInvalidIndex, 0, info) && info.exists && info.enabled);
    bridge::LayerDetailPOD det{};
    AUREA_CHECK(e.query_layer_detail(*id, det));
    const f32 base = det.position[0];
    AUREA_CHECK_NEAR(info.value, base, 1e-3f);   // (value lido antes do detalhe: mesmo quadro)
    // Mostrado na UI = valor resultante.
    AUREA_CHECK(e.set_expression(*id, px, kInvalidIndex, 0, "value + 100").ok());
    Engine::ExpressionInfo info2;
    (void)e.query_expression(*id, px, kInvalidIndex, 0, info2);
    const Layer* l = e.project()->timeline().composition(e.project()->timeline().current())->layer(LayerId::unpack(*id));
    AUREA_CHECK_NEAR(info2.value, l->transform.position.x + 100.0f, 1e-3f);
    // Erro de sintaxe: fica gravado, diag diz onde.
    AUREA_CHECK(e.set_expression(*id, static_cast<u32>(TrackProperty::Opacity), kInvalidIndex, 0, "50 +", &diag).ok());
    AUREA_CHECK(!diag.ok);
    u32 rows[16 * 4]{};
    AUREA_CHECK_EQ(e.query_expressions(*id, rows, 16), 2u);
    // Desfazer tira a última; liga/desliga.
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(e.query_expressions(*id, rows, 16), 1u);
    AUREA_CHECK(e.set_expression_enabled(*id, px, kInvalidIndex, 0, false));
    Engine::ExpressionInfo off;
    (void)e.query_expression(*id, px, kInvalidIndex, 0, off);
    AUREA_CHECK(!off.enabled && off.source == "value + 100");
    AUREA_CHECK_NEAR(off.value, l->transform.position.x, 1e-3f);
    AUREA_CHECK(e.set_expression_enabled(*id, px, kInvalidIndex, 0, true));
    // Slider de verdade (efeito adicionado pelo comando) dirigindo a opacidade.
    // Salvar e reabrir: fonte, ligada e o valor.
    AUREA_CHECK(e.set_expression(*id, static_cast<u32>(TrackProperty::RotationZ), kInvalidIndex, 0, "time * 90").ok());
    AUREA_CHECK(e.set_expression_enabled(*id, static_cast<u32>(TrackProperty::RotationZ), kInvalidIndex, 0, false));
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_expr.aurea";
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    Engine::ExpressionInfo re, rr;
    AUREA_CHECK(e.query_expression(*id, px, kInvalidIndex, 0, re) && re.exists && re.enabled);
    AUREA_CHECK(re.source == "value + 100");
    const Layer* l2 = e.project()->timeline().composition(e.project()->timeline().current())->layer(LayerId::unpack(*id));
    AUREA_CHECK(l2 != nullptr);
    if (l2) AUREA_CHECK_NEAR(re.value, l2->transform.position.x + 100.0f, 1e-3f);
    AUREA_CHECK(e.query_expression(*id, static_cast<u32>(TrackProperty::RotationZ), kInvalidIndex, 0, rr) && rr.exists);
    AUREA_CHECK(!rr.enabled && rr.source == "time * 90");
    AUREA_CHECK_EQ(e.query_expressions(*id, rows, 16), 2u);
    // Remover (fonte vazia).
    AUREA_CHECK(e.set_expression(*id, px, kInvalidIndex, 0, "").ok());
    AUREA_CHECK_EQ(e.query_expressions(*id, rows, 16), 1u);
    std::remove(path.c_str());
    e.shutdown();
    std::printf("API: gravar/ligar/desligar/desfazer; salvo e reaberto com fonte + estado; ");
}

// -----------------------------------------------------------------------------
// Custo
// -----------------------------------------------------------------------------
AUREA_TEST(Expr, ThousandLayersWiggleCostPerFrame) {
    Rig rig;
    constexpr u32 kLayers = 1000;
    std::vector<LayerId> ids;
    for (u32 i = 0; i < kLayers; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "L%u", i);
        LayerId id;
        Layer* l = rig.add(name, &id);
        set_expr(l->tracks.get_or_create(TrackProperty::PositionX), "wiggle(2, 30)");
        set_expr(l->tracks.get_or_create(TrackProperty::PositionY), "wiggle(2, 30)");
        ids.push_back(id);
    }
    // Ponteiros só depois de criar todas (a tabela de camadas realoca ao crescer).
    std::vector<Layer*> layers;
    for (LayerId id : ids) layers.push_back(rig.comp->layer(id));
    constexpr u32 kFrames = 120;
    f64 checksum = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (u32 f = 0; f < kFrames; ++f) {
        const expr::Scope scope(rig.tl);   // um quadro do renderer
        for (Layer* l : layers) {
            checksum += l->tracks.find(TrackProperty::PositionX)->value_or(FrameIndex{f}, 0.0f);
            checksum += l->tracks.find(TrackProperty::PositionY)->value_or(FrameIndex{f}, 0.0f);
        }
    }
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count() / kFrames;
    // Referência: as mesmas 2000 leituras só com keyframes.
    for (Layer* l : layers) {
        for (TrackProperty p : {TrackProperty::PositionX, TrackProperty::PositionY}) {
            Track* t = l->tracks.find(p);
            t->expression.reset();
            (void)t->set(FrameIndex{0}, 0.0f);
            (void)t->set(FrameIndex{100}, 50.0f);
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    for (u32 f = 0; f < kFrames; ++f) {
        for (Layer* l : layers) {
            checksum += l->tracks.find(TrackProperty::PositionX)->value_or(FrameIndex{f}, 0.0f);
            checksum += l->tracks.find(TrackProperty::PositionY)->value_or(FrameIndex{f}, 0.0f);
        }
    }
    const f64 msKeys = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t1).count() / kFrames;
    AUREA_CHECK(std::isfinite(checksum));
    AUREA_CHECK(ms < 16.0);   // cabe folgado num quadro de 60 Hz no host
    std::printf("1000 camadas x wiggle em X e Y (2000 expressoes): %.3f ms/quadro (%.2f us/expr); so keyframes: %.3f ms ",
                ms, ms * 1000.0 / (kLayers * 2), msKeys);
}
