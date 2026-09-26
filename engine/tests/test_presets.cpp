// =============================================================================
//  Presets (Fase 7F): JSON versionado de efeitos, texto, animação, legenda e
//  curva — ida e volta, retemporização, leitura defensiva e desfazer.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/project/Presets.hpp"
#include "aurea/text/TextAnimator.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace aurea;
using presets::PresetKind;

namespace {

struct PresetRig {
    Engine e;
    PresetRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    }
    ~PresetRig() { e.shutdown(); }
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
    u32 add_effect(u64 id, const char* key) {
        Command fx;
        fx.type = CommandType::EffectAdd;
        fx.effect_add.layer = LayerId::unpack(id);
        fx.effect_add.effectType = effect_type_id(key);
        fx.effect_add.index = kInvalidIndex;
        AUREA_CHECK(e.apply_command(fx).ok());
        return L(id)->effects.back().id;
    }
    void seek(i64 frame) {
        Command s;
        s.type = CommandType::PlaybackSeek;
        s.seek.time = TickNs{frame * 1'000'000'000 / 30 + 1'000};
        AUREA_CHECK(e.apply_command(s).ok());
    }
    void undo() {
        Command u;
        u.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(u).ok());
    }
};

bool same_key(const Keyframe& a, const Keyframe& b) {
    return a.value == b.value && a.interp == b.interp && a.bx1 == b.bx1 && a.by1 == b.by1 && a.bx2 == b.bx2 && a.by2 == b.by2
        && a.tangentIn == b.tangentIn && a.tangentOut == b.tangentOut && a.easingPreset == b.easingPreset;
}

/// Trilhas `prop` da camada com o índice de efeito/animador `idx`, em ordem de
/// parâmetro (para comparar duas camadas).
std::vector<const Track*> tracks_of(const Layer& l, TrackProperty prop, u32 idx) {
    std::vector<const Track*> out;
    for (u32 i = 0; i < l.tracks.size(); ++i) {
        const Track& t = l.tracks.at(i);
        if (t.property == prop && t.effectIndex == idx) out.push_back(&t);
    }
    std::sort(out.begin(), out.end(), [](const Track* a, const Track* b) { return a->effectParamIndex < b->effectParamIndex; });
    return out;
}

} // namespace

AUREA_TEST(Presets, JsonReaderAndWriterRoundTrip) {
    json::Writer w;
    w.begin_object().key("a").value(0.1f).key("s").value("x\"y\n\xC3\xA9").key("l").begin_array().value(i64{-3}).value(true).end_array().end_object();
    json::Value v;
    AUREA_CHECK(json::parse(w.str(), v));
    AUREA_CHECK(v.get("a") && static_cast<f32>(v.get("a")->number) == 0.1f);   // f32 exato na volta
    AUREA_CHECK(v.get("s") && v.get("s")->string == "x\"y\n\xC3\xA9");
    AUREA_CHECK(v.get("l") && v.get("l")->array.size() == 2 && v.get("l")->array[0].number == -3.0);
    json::Value u;
    AUREA_CHECK(json::parse(R"({"k":"\u00e9\ud83d\ude00"})", u));
    AUREA_CHECK(u.get("k") && u.get("k")->string == "\xC3\xA9\xF0\x9F\x98\x80");
    const char* bad[] = {"", "{", "[1,]", "{\"a\":01}", "{\"a\":1}x", "nul", "\"\\x\"", "{\"a\" 1}", "[1e999]"};
    for (const char* b : bad) {
        json::Value x;
        AUREA_CHECK_MSG(!json::parse(b, x), b);
    }
    std::string deep(200, '[');
    json::Value d;
    AUREA_CHECK(!json::parse(deep, d));   // profundidade limitada, sem estourar pilha
}

AUREA_TEST(Presets, EffectsRoundTripOnAnotherLayerKeepsParamsAndRelativeKeys) {
    PresetRig r;
    const u64 a = *r.e.add_shape(0);
    const u64 b = *r.e.add_shape(3);
    r.range(a, 10, 100, 0);   // começa em 10
    r.range(b, 40, 120, 5);   // começa em 40, conteúdo deslocado 5
    const u32 blur = r.add_effect(a, effect_keys::kGaussianBlur);
    const u32 glow = r.add_effect(a, effect_keys::kGlow);
    Layer* la = r.L(a);
    la->effects[0].params[0].constant.v[0] = 6.5f;
    la->effects[1].params[0].constant.v[0] = 0.8f;
    la->effects[1].enabled = false;
    // Keyframes: local(início) + 5 e + 20, com bézier própria.
    Track& t = la->tracks.get_or_create(TrackProperty::EffectParam, blur, param_track_key(0, 0));
    const i64 s0 = la->local_time(la->start).value;
    t.set(FrameIndex{s0 + 5}, 2.0f, Interpolation::Bezier);
    t.set(FrameIndex{s0 + 20}, 9.0f, Interpolation::Linear);
    t.keys[0].bx1 = 0.1f; t.keys[0].by1 = 0.2f; t.keys[0].bx2 = 0.7f; t.keys[0].by2 = 0.95f;
    la->tracks.get_or_create(TrackProperty::EffectParam, glow, param_track_key(0, 0)).set(FrameIndex{s0 + 12}, 0.3f);

    const std::string js = r.e.save_preset(a, PresetKind::Effects, "Brilho e desfoque");
    AUREA_CHECK(!js.empty());
    AUREA_CHECK(js.find("\"aurea.blur.gaussian\"") != std::string::npos);
    // Ler e escrever de novo dá o MESMO texto (formato estável).
    presets::Preset p;
    AUREA_CHECK(presets::parse(js, p, &r.e.effects()));
    AUREA_CHECK_EQ(presets::write(p, &r.e.effects()), js);
    AUREA_CHECK(p.name == "Brilho e desfoque");

    const u32 depth = r.e.history().depth();
    AUREA_CHECK(r.e.apply_preset(b, js));
    AUREA_CHECK_EQ(r.e.history().depth(), depth + 1);   // um passo de desfazer
    la = r.L(a);
    const Layer* lb = r.L(b);
    AUREA_CHECK_EQ(lb->effects.size(), usize{2});
    for (u32 i = 0; i < 2; ++i) {
        const EffectInstance& ea = la->effects[i];
        const EffectInstance& eb = lb->effects[i];
        AUREA_CHECK(ea.type == eb.type && ea.enabled == eb.enabled);
        AUREA_CHECK_EQ(ea.params.size(), eb.params.size());
        for (usize k = 0; k < ea.params.size() && k < eb.params.size(); ++k) {
            AUREA_CHECK(ea.params[k].constant == eb.params[k].constant && ea.params[k].source == eb.params[k].source);
        }
        // Keyframes iguais, no MESMO tempo relativo ao início de cada camada.
        const auto ta = tracks_of(*la, TrackProperty::EffectParam, ea.id);
        const auto tb = tracks_of(*lb, TrackProperty::EffectParam, eb.id);
        AUREA_CHECK_EQ(ta.size(), tb.size());
        for (usize k = 0; k < ta.size() && k < tb.size(); ++k) {
            AUREA_CHECK_EQ(ta[k]->effectParamIndex, tb[k]->effectParamIndex);
            AUREA_CHECK_EQ(ta[k]->keys.size(), tb[k]->keys.size());
            for (usize q = 0; q < ta[k]->keys.size() && q < tb[k]->keys.size(); ++q) {
                AUREA_CHECK(same_key(ta[k]->keys[q], tb[k]->keys[q]));
                AUREA_CHECK_EQ(ta[k]->keys[q].time.value - la->offset.value, tb[k]->keys[q].time.value - lb->offset.value);
            }
        }
    }
    // Na timeline: o keyframe de a em 10+5 = 15 cai em b em 40+5 = 45.
    const auto tb0 = tracks_of(*lb, TrackProperty::EffectParam, lb->effects[0].id);
    AUREA_CHECK(!tb0.empty() && lb->timeline_time(tb0[0]->keys[0].time).value == 45);
    // Aplicar ACRESCENTA; o próximo efeito adicionado não repete id.
    AUREA_CHECK(r.e.apply_preset(b, js));
    const u32 extra = r.add_effect(b, effect_keys::kSharpen);
    lb = r.L(b);
    AUREA_CHECK_EQ(lb->effects.size(), usize{5});
    for (usize i = 0; i + 1 < lb->effects.size(); ++i) AUREA_CHECK(lb->effects[i].id != extra);
}

// Um efeito só (o segundo de dois): o preset leva só ele, com os parâmetros
// e os keyframes, e aplicado em outra camada não traz o vizinho.
AUREA_TEST(Presets, SingleEffectPresetCapturesOnlyThatEffect) {
    PresetRig r;
    const u64 a = *r.e.add_shape(0);
    const u64 b = *r.e.add_shape(3);
    r.range(a, 10, 100, 0);
    r.range(b, 40, 120, 5);
    const u32 blur = r.add_effect(a, effect_keys::kGaussianBlur);
    const u32 glow = r.add_effect(a, effect_keys::kGlow);
    Layer* la = r.L(a);
    la->effects[0].params[0].constant.v[0] = 6.5f;
    la->effects[1].params[1].constant.v[0] = 44.0f;   // raio do brilho
    la->effects[1].params[3].constant = ParamValue::color(1.0f, 0.5f, 0.25f, 1.0f);
    const i64 s0 = la->local_time(la->start).value;
    la->tracks.get_or_create(TrackProperty::EffectParam, blur, param_track_key(0, 0)).set(FrameIndex{s0 + 3}, 1.0f);
    Track& g = la->tracks.get_or_create(TrackProperty::EffectParam, glow, param_track_key(2, 0));
    g.set(FrameIndex{s0 + 4}, 0.5f, Interpolation::EaseOut);
    g.set(FrameIndex{s0 + 16}, 3.0f);

    const std::string js = r.e.save_effect_preset(a, glow, "Só o brilho");
    AUREA_CHECK(!js.empty());
    AUREA_CHECK(js.find("\"aurea.light.glow\"") != std::string::npos);
    AUREA_CHECK(js.find("\"aurea.blur.gaussian\"") == std::string::npos);
    AUREA_CHECK(r.e.save_effect_preset(a, 9999, "x").empty());          // efeito que não existe
    AUREA_CHECK(r.e.save_effect_preset(0xDEADBEEFull, glow, "x").empty());   // camada que não existe
    // O pedido de pilha inteira continua igual.
    presets::Preset whole;
    AUREA_CHECK(presets::parse(r.e.save_preset(a, PresetKind::Effects, "tudo"), whole, &r.e.effects()));
    AUREA_CHECK_EQ(whole.effects.size(), usize{2});

    presets::Preset p;
    AUREA_CHECK(presets::parse(js, p, &r.e.effects()));
    AUREA_CHECK(p.kind == PresetKind::Effects && p.name == "Só o brilho");
    AUREA_CHECK_EQ(p.effects.size(), usize{1});
    AUREA_CHECK_EQ(p.effectTracks.size(), usize{1});
    AUREA_CHECK_EQ(presets::write(p, &r.e.effects()), js);

    AUREA_CHECK(r.e.apply_preset(b, js));
    la = r.L(a);
    const Layer* lb = r.L(b);
    AUREA_CHECK_EQ(lb->effects.size(), usize{1});
    if (lb->effects.size() != 1) return;
    const EffectInstance& src = la->effects[1];
    const EffectInstance& dst = lb->effects[0];
    AUREA_CHECK(dst.type == effect_type_id(effect_keys::kGlow));
    AUREA_CHECK_EQ(src.params.size(), dst.params.size());
    for (usize k = 0; k < src.params.size() && k < dst.params.size(); ++k) AUREA_CHECK(src.params[k].constant == dst.params[k].constant);
    const auto ta = tracks_of(*la, TrackProperty::EffectParam, glow);
    const auto tb = tracks_of(*lb, TrackProperty::EffectParam, dst.id);
    AUREA_CHECK_EQ(ta.size(), usize{1});
    AUREA_CHECK_EQ(tb.size(), usize{1});
    if (ta.size() != 1 || tb.size() != 1) return;
    AUREA_CHECK_EQ(tb[0]->effectParamIndex, param_track_key(2, 0));
    AUREA_CHECK_EQ(tb[0]->keys.size(), usize{2});
    for (usize q = 0; q < ta[0]->keys.size() && q < tb[0]->keys.size(); ++q) {
        AUREA_CHECK(same_key(ta[0]->keys[q], tb[0]->keys[q]));
        AUREA_CHECK_EQ(ta[0]->keys[q].time.value - la->offset.value, tb[0]->keys[q].time.value - lb->offset.value);
    }
    // Nenhuma trilha do desfoque veio junto.
    u32 effectTracks = 0;
    for (u32 i = 0; i < lb->tracks.size(); ++i) if (lb->tracks.at(i).property == TrackProperty::EffectParam) ++effectTracks;
    AUREA_CHECK_EQ(effectTracks, 1u);
}

AUREA_TEST(Presets, TextStylePresetReproducesFieldsAndAnimators) {
    PresetRig r;
    const u64 a = *r.e.add_text("Origem");
    const u64 b = *r.e.add_text("Destino com outro texto");
    const u64 shape = *r.e.add_shape(0);
    Layer* la = r.L(a);
    TextData& t = la->text;
    t.fontFamily = "serif";
    t.fontWeight = 700;
    t.fontItalic = true;
    t.size = 96.0f;
    t.color = Vec4{0.9f, 0.2f, 0.1f, 1.0f};
    t.strokeWidth = 4.0f;
    t.strokeColor = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
    t.alignment = 1;
    t.lineHeight = 1.5f;
    t.tracking = 3.0f;
    t.boxMode = 1;
    t.box = Rect{0, 0, 640, 300};
    t.background = true;
    t.backgroundColor = Vec4{0.1f, 0.1f, 0.1f, 0.7f};
    t.backgroundPadding = 20.0f;
    t.backgroundRadius = 12.0f;
    t.shadow = true;
    t.shadowColor = Vec4{0, 0, 0, 0.5f};
    t.shadowOffset = Vec2{3, 8};
    t.shadowBlur = 9.0f;
    AUREA_CHECK(r.e.apply_text_preset(a, 1));   // Bounce: 2+ animadores com keyframes
    la = r.L(a);
    AUREA_CHECK(!la->text.animators.empty());

    const std::string js = r.e.save_preset(a, PresetKind::Text, "Título");
    AUREA_CHECK(!js.empty());
    AUREA_CHECK(r.e.save_preset(shape, PresetKind::Text, "x").empty());   // forma não tem texto
    AUREA_CHECK(!r.e.apply_preset(shape, js));                             // e não recebe

    AUREA_CHECK(r.e.apply_preset(b, js));
    la = r.L(a);
    const Layer* lb = r.L(b);
    const TextData& s = la->text;
    const TextData& d = lb->text;
    AUREA_CHECK(d.content == "Destino com outro texto");   // conteúdo fica
    AUREA_CHECK(d.fontFamily == s.fontFamily && d.fontWeight == s.fontWeight && d.fontItalic == s.fontItalic);
    AUREA_CHECK(d.size == s.size && d.color == s.color && d.strokeWidth == s.strokeWidth && d.strokeColor == s.strokeColor);
    AUREA_CHECK(d.alignment == s.alignment && d.lineHeight == s.lineHeight && d.tracking == s.tracking);
    AUREA_CHECK(d.boxMode == s.boxMode && d.box.w == s.box.w && d.box.h == s.box.h && d.autoSize == s.autoSize);
    AUREA_CHECK(d.background == s.background && d.backgroundColor == s.backgroundColor);
    AUREA_CHECK(d.backgroundPadding == s.backgroundPadding && d.backgroundRadius == s.backgroundRadius);
    AUREA_CHECK(d.shadow == s.shadow && d.shadowColor == s.shadowColor && d.shadowOffset == s.shadowOffset && d.shadowBlur == s.shadowBlur);
    AUREA_CHECK_EQ(d.animators.size(), s.animators.size());
    for (usize i = 0; i < s.animators.size() && i < d.animators.size(); ++i) {
        const TextAnimator& x = s.animators[i];
        const TextAnimator& y = d.animators[i];
        AUREA_CHECK(x.name == y.name && x.props == y.props && x.enabled == y.enabled);
        AUREA_CHECK(x.selector.start == y.selector.start && x.selector.end == y.selector.end && x.selector.shape == y.selector.shape
                    && x.selector.basedOn == y.selector.basedOn && x.selector.amount == y.selector.amount);
        AUREA_CHECK(x.position == y.position && x.scale == y.scale && x.rotation == y.rotation && x.opacity == y.opacity);
        AUREA_CHECK(x.fill == y.fill && x.stroke == y.stroke && x.blur == y.blur);
        const auto ta = tracks_of(*la, TrackProperty::TextAnimParam, static_cast<u32>(i));
        const auto tb = tracks_of(*lb, TrackProperty::TextAnimParam, static_cast<u32>(i));
        AUREA_CHECK_EQ(ta.size(), tb.size());
        for (usize k = 0; k < ta.size() && k < tb.size(); ++k) {
            AUREA_CHECK_EQ(ta[k]->keys.size(), tb[k]->keys.size());
            for (usize q = 0; q < ta[k]->keys.size() && q < tb[k]->keys.size(); ++q) {
                AUREA_CHECK(same_key(ta[k]->keys[q], tb[k]->keys[q]));
                AUREA_CHECK_EQ(ta[k]->keys[q].time.value - la->offset.value, tb[k]->keys[q].time.value - lb->offset.value);
            }
        }
    }
    // Só a animação: o estilo do destino fica.
    const u64 c = *r.e.add_text("Outro");
    const f32 sizeC = r.L(c)->text.size;
    const std::string anim = r.e.save_preset(a, PresetKind::Text, "Só animação", presets::kTextAnimators);
    AUREA_CHECK(anim.find("\"style\"") == std::string::npos);
    AUREA_CHECK(r.e.apply_preset(c, anim));
    AUREA_CHECK(r.L(c)->text.size == sizeC && r.L(c)->text.animators.size() == s.animators.size());
}

AUREA_TEST(Presets, AnimationPresetIsRetimedToThePlayheadAndScaled) {
    PresetRig r;
    const u64 a = *r.e.add_null(false);
    const u64 b = *r.e.add_null(false);
    r.range(a, 0, 90, 0);
    r.range(b, 0, 90, 0);
    Layer* la = r.L(a);
    la->transform.position = Vec3{100, 50, 0};
    Track& px = la->tracks.get_or_create(TrackProperty::PositionX);
    px.set(FrameIndex{10}, 100.0f, Interpolation::Bezier);
    px.set(FrameIndex{25}, 400.0f);
    Track& op = la->tracks.get_or_create(TrackProperty::Opacity);
    op.set(FrameIndex{10}, 0.0f);
    op.set(FrameIndex{25}, 1.0f);
    const std::string js = r.e.save_preset(a, PresetKind::Animation, "Entrar");
    AUREA_CHECK(!js.empty());
    presets::Preset p;
    AUREA_CHECK(presets::parse(js, p));
    AUREA_CHECK_EQ(p.span, i64{15});

    r.L(b)->transform.position = Vec3{500, 60, 0};
    r.seek(40);
    AUREA_CHECK_EQ(r.e.project()->timeline().playhead().value, i64{40});
    AUREA_CHECK(r.e.apply_preset(b, js));
    const Layer* lb = r.L(b);
    const Track* bx = lb->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(bx && bx->keys.size() == 2);
    if (bx && bx->keys.size() == 2) {
        // Começa no cabeçote; posição RELATIVA: parte de onde b está (500).
        AUREA_CHECK_EQ(bx->keys[0].time.value, i64{40});
        AUREA_CHECK_EQ(bx->keys[1].time.value, i64{55});
        AUREA_CHECK_NEAR(bx->keys[0].value, 500.0f, 1e-4f);
        AUREA_CHECK_NEAR(bx->keys[1].value, 800.0f, 1e-4f);
        AUREA_CHECK(bx->keys[0].interp == Interpolation::Bezier);
    }
    const Track* bo = lb->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(bo && bo->keys.size() == 2 && bo->keys[0].time.value == 40 && bo->keys[1].value == 1.0f);

    // Esticada para 30 quadros: 40 → 70.
    const u64 c = *r.e.add_null(false);
    r.range(c, 0, 90, 0);
    r.seek(40);
    AUREA_CHECK(r.e.apply_preset(c, js, 30));
    const Track* cx = r.L(c)->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(cx && cx->keys.size() == 2 && cx->keys[0].time.value == 40 && cx->keys[1].time.value == 70);

    // Cabeçote fora da camada: começa no início dela.
    const u64 d = *r.e.add_null(false);
    r.range(d, 60, 90, 0);
    r.seek(10);
    AUREA_CHECK(r.e.apply_preset(d, js));
    const Layer* ld = r.L(d);
    const Track* dx = ld->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(dx && !dx->keys.empty() && ld->timeline_time(dx->keys[0].time).value == 60);

    // Outro fps: 15 quadros a 30 fps = 30 quadros a 60 fps.
    presets::Preset q = p;
    q.fps = 15.0;
    Layer tmp;
    tmp.kind = LayerKind::Null;
    AUREA_CHECK(presets::apply(q, tmp, 0, 0, 30.0));
    const Track* qx = tmp.tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(qx && qx->keys.size() == 2 && qx->keys[1].time.value == 30);
}

AUREA_TEST(Presets, MalformedPresetIsRejectedWithoutMutation) {
    PresetRig r;
    const u64 a = *r.e.add_shape(0);
    r.add_effect(a, effect_keys::kGaussianBlur);
    const std::string good = r.e.save_preset(a, PresetKind::Effects, "ok");
    AUREA_CHECK(!good.empty());
    const u64 b = *r.e.add_shape(0);
    const u32 depth = r.e.history().depth();
    const usize fx = r.L(b)->effects.size();
    const u32 tracks = r.L(b)->tracks.size();
    std::string truncated = good.substr(0, good.size() / 2);
    std::string future = good;
    future.replace(future.find("\"aurea_preset\":1"), 16, "\"aurea_preset\":99");
    std::string unknown = good;
    unknown.replace(unknown.find("aurea.blur.gaussian"), 19, "aurea.nao.existe.xx");
    std::string badKey = good;
    badKey.replace(badKey.find("\"effects\""), 9, "\"efeitos\"");
    const std::string cases[] = {
        "", "{}", "[]", "null", truncated, future, unknown, badKey,
        R"({"aurea_preset":1,"kind":"planeta","name":"x"})",
        R"({"aurea_preset":1,"kind":"effects","effects":[{"key":"aurea.blur.gaussian","tracks":[{"index":0,"keys":[[1.5,2]]}]}]})",
        R"({"aurea_preset":1,"kind":"effects","effects":[{"key":"aurea.blur.gaussian","tracks":[{"index":0,"keys":[[1,2,99]]}]}]})",
        R"({"aurea_preset":1,"kind":"effects","effects":[{"key":"aurea.blur.gaussian","params":[{"index":0,"v":[1,2]}]}]})",
        R"({"aurea_preset":1,"kind":"animation","tracks":[{"prop":"cor","keys":[[0,1]]}]})",
        R"({"aurea_preset":1,"kind":"animation","tracks":[{"prop":"opacity","keys":[]}]})",
        R"({"aurea_preset":1,"kind":"text","style":{"size":-5}})",
        R"({"aurea_preset":1,"kind":"text","animators":[],"tracks":[{"animator":3,"param":0,"keys":[[0,1]]}]})",
        R"({"aurea_preset":1,"kind":"text","style":{"fontFamily":"x"}})",   // válido, mas camada é forma
        R"({"aurea_preset":1,"kind":"curve","curve":{"x1":0.4,"y1":0,"x2":0.6,"y2":1}})",   // válido, não é de camada
        std::string(100000, '['),
    };
    for (const std::string& c : cases) {
        std::string err;
        AUREA_CHECK_MSG(!r.e.apply_preset(b, c, 0, &err), c.substr(0, 60).c_str());
        AUREA_CHECK(!err.empty());
    }
    AUREA_CHECK_EQ(r.e.history().depth(), depth);   // nenhum passo de desfazer vazio
    AUREA_CHECK_EQ(r.L(b)->effects.size(), fx);
    AUREA_CHECK_EQ(r.L(b)->tracks.size(), tracks);
    AUREA_CHECK(!r.e.apply_preset(0xDEADBEEFull, good));   // camada inexistente
}

AUREA_TEST(Presets, UndoRestoresTheLayer) {
    PresetRig r;
    const u64 a = *r.e.add_shape(0);
    r.add_effect(a, effect_keys::kGaussianBlur);
    const std::string js = r.e.save_preset(a, PresetKind::Effects, "x");
    const u64 t = *r.e.add_text("Oi");
    const u64 t2 = *r.e.add_text("Estilo");
    r.L(t2)->text.size = 150.0f;
    const std::string style = r.e.save_preset(t2, PresetKind::Text, "grande", presets::kTextStyle);
    const f32 size0 = r.L(t)->text.size;
    AUREA_CHECK(r.e.apply_preset(t, js));
    AUREA_CHECK(r.e.apply_preset(t, style));
    AUREA_CHECK(r.L(t)->effects.size() == 1 && r.L(t)->text.size == 150.0f);
    r.undo();
    AUREA_CHECK(r.L(t)->text.size == size0 && r.L(t)->effects.size() == 1);
    r.undo();
    AUREA_CHECK(r.L(t)->effects.empty());
}

// Os presets que o app embarca (android/app/src/main/assets/presets/*.json,
// uma lista por tipo) são lidos pelo MESMO leitor e aplicados de verdade.
AUREA_TEST(Presets, BuiltinAppPresetsAreValid) {
    PresetRig r;
    const u64 shape = *r.e.add_shape(0);
    const u64 null = *r.e.add_null(false);
    const std::pair<const char*, PresetKind> files[] = {
        {"efeitos.json", PresetKind::Effects}, {"animacao.json", PresetKind::Animation},
        {"curva.json", PresetKind::Curve}, {"legenda.json", PresetKind::Caption},
    };
    u32 total = 0;
    for (const auto& [file, kind] : files) {
        const std::string path = std::string(AUREA_APP_PRESETS_DIR) + "/" + file;
        std::FILE* f = std::fopen(path.c_str(), "rb");
        AUREA_CHECK_MSG(f != nullptr, path.c_str());
        if (!f) continue;
        std::string text;
        char buf[4096];
        for (usize n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
        std::fclose(f);
        json::Value list;
        AUREA_CHECK_MSG(json::parse(text, list) && list.is_array() && !list.array.empty(), file);
        for (const json::Value& v : list.array) {
            presets::Preset p;
            std::string err;
            const bool ok = presets::parse_value(v, p, &r.e.effects(), &err);
            AUREA_CHECK_MSG(ok, (std::string(file) + ": " + err).c_str());
            AUREA_CHECK(p.kind == kind && !p.name.empty());
            ++total;
            if (!ok || kind == PresetKind::Curve || kind == PresetKind::Caption) continue;
            // Aplica pelo motor (texto do preset re-escrito, como o app manda).
            const std::string js = presets::write(p, &r.e.effects());
            AUREA_CHECK_MSG(r.e.apply_preset(kind == PresetKind::Effects ? shape : null, js, 0, &err), (p.name + ": " + err).c_str());
        }
    }
    std::printf("    %u presets embarcados validos\n", total);
    AUREA_CHECK(total >= 20);
}

AUREA_TEST(Presets, OminoCustomPaletteAndCCStackSaveApplyUndo) {
    PresetRig r;
    const auto source=*r.e.add_shape(0), target=*r.e.add_shape(0);
    for (const char* key : {effect_keys::kSharpen,effect_keys::kUnsharp,effect_keys::kExposure,
                           effect_keys::kBrightnessContrast,effect_keys::kSaturation,"aurea.stylize.omino_diffusion"})
        r.add_effect(source,key);
    auto* layer=r.L(source);
    layer->effects[0].params[0].constant.v[0]=50;
    layer->effects[1].params[0].constant.v[0]=15;
    layer->effects[1].params[1].constant.v[0]=30;
    auto& omino=layer->effects.back();
    omino.params[6].constant.v[0]=4;
    omino.params[11].constant=ParamValue::color(.1f,.2f,.3f,1);
    layer->tracks.get_or_create(TrackProperty::EffectParam,omino.id,param_track_key(2,0)).set(FrameIndex{12},90);
    const auto saved=r.e.save_preset(source,PresetKind::Effects,"Meu CC + difusão");
    AUREA_CHECK(r.e.apply_preset(target,saved));
    const auto* applied=r.L(target);
    AUREA_CHECK_EQ(applied->effects.size(),usize{6});
    if(applied->effects.size()!=6) return;
    for(usize i=0;i<6;++i) {
        AUREA_CHECK_EQ(applied->effects[i].type,r.L(source)->effects[i].type);
        for(usize p=0;p<applied->effects[i].params.size();++p)
            AUREA_CHECK(applied->effects[i].params[p].constant==r.L(source)->effects[i].params[p].constant);
    }
    AUREA_CHECK_EQ(tracks_of(*applied,TrackProperty::EffectParam,applied->effects.back().id).size(),usize{1});
    r.undo();
    AUREA_CHECK(r.L(target)->effects.empty());
}

AUREA_TEST(Presets, CaptionAndCurvePresetsRoundTrip) {
    text::CaptionOptions o;
    o.mode = 1;
    o.maxWords = 3;
    o.style = 4;
    o.highlight = false;
    o.posY = 0.2f;
    o.sizeFrac = 0.09f;
    o.highlightColor = Vec4{0.1f, 0.9f, 0.3f, 1.0f};
    const std::string js = presets::make_caption_preset("Karaokê alto", o, false);
    presets::Preset p;
    AUREA_CHECK(presets::parse(js, p));
    AUREA_CHECK(p.kind == PresetKind::Caption && p.name == "Karaokê alto" && !p.removeFillers);
    AUREA_CHECK(p.caption.mode == 1 && p.caption.maxWords == 3 && p.caption.style == 4 && !p.caption.highlight);
    AUREA_CHECK(p.caption.posY == 0.2f && p.caption.sizeFrac == 0.09f && p.caption.highlightColor == o.highlightColor);

    const std::string cv = presets::make_curve_preset("Saída forte", Interpolation::Bezier, 0.16f, 1.0f, 0.3f, 1.0f);
    presets::Preset q;
    AUREA_CHECK(presets::parse(cv, q));
    AUREA_CHECK(q.kind == PresetKind::Curve && q.curveInterp == Interpolation::Bezier);
    AUREA_CHECK(q.x1 == 0.16f && q.y1 == 1.0f && q.x2 == 0.3f && q.y2 == 1.0f);
    presets::Preset bad;
    AUREA_CHECK(!presets::parse(R"({"aurea_preset":1,"kind":"curve","curve":{"x1":4}})", bad));
    AUREA_CHECK(!presets::parse(R"({"aurea_preset":1,"kind":"caption","caption":{"style":99}})", bad));
}
