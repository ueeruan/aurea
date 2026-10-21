// =============================================================================
//  Os dois tipos de efeito de plugin.
//
//  SHADER. Os passes do autor no FrameGraph, com os pipelines do vértice de
//  tela cheia do motor + o fragment shader do plugin (ShaderId dinâmico).
//  Toda textura que o shader vê — a fonte, as entradas nomeadas, os alvos
//  intermediários — cobre o MESMO retângulo (a região de trabalho do efeito):
//  o `uv` 0..1 é o mesmo em todas, qualquer que seja a escala do passe. Quando
//  o plugin declara margem (lê além da caixa) ou trabalha em sRGB, um passe de
//  preparo do motor (plugin_prepare.frag) copia a entrada para essa região,
//  com borda transparente, e converte a cor.
//
//  STACK. Uma pilha de embutidos. No projeto, o EffectGraph EXPANDE o plugin
//  nos embutidos durante o planejamento (Effect::expand_stack): fusão de cor,
//  identidade, recursos e margens dos embutidos valem como se a pessoa os
//  tivesse aplicado à mão. `build` aqui é só o caminho da prévia do catálogo
//  (e do teste de instalação), que monta um efeito direto, sem planejamento.
// =============================================================================
#include "PluginInternal.hpp"
#include "aurea/effects/Puppet.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "effects/builtin/BuiltinEffects.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace aurea::plugins {

namespace {

constexpr f32 kDegToRad = 3.14159265358979323846f / 180.0f;

// Nomes estáticos: as medições de GPU por passe guardam o ponteiro.
constexpr const char* kPassNames[kMaxPasses] = {
    "plugin-passe-1", "plugin-passe-2", "plugin-passe-3", "plugin-passe-4",
    "plugin-passe-5", "plugin-passe-6", "plugin-passe-7", "plugin-passe-8",
};

/// O bloco `aurea` do cabeçalho (std140: 20 vec4).
struct PluginUniforms {
    Vec4 resolution;
    Vec4 time;
    Vec4 layer;
    Vec4 region;
    Vec4 params[kMaxParams];
};
static_assert(sizeof(PluginUniforms) == 320, "layout std140 do bloco aurea dos plugins");
static_assert(sizeof(PluginUniforms) <= binding::kMaxUniformBytes);

/// O bloco do color_stack.frag (o mesmo do EffectGraph) para UM embutido por pixel.
struct ColorStackUniforms {
    f32 header[4];
    f32 ops[12 * 16];
};

/// Textos dos parâmetros com vida igual à do efeito (ParamSpec guarda ponteiros).
struct ParamStrings {
    std::string label;
    std::vector<std::string> options;
    std::vector<const char*> optionPtrs;
};

void declare_from_manifest(const Manifest& m, const std::vector<ParamStrings>& strings, ParameterRegistry& p) {
    for (usize i = 0; i < m.params.size(); ++i) {
        const ParamDecl& d = m.params[i];
        const ParamStrings& s = strings[i];
        ParamSpec spec;
        spec.id = d.id.c_str();
        spec.label = s.label.c_str();
        spec.flags = d.animatable ? kParamAnimatable : kParamNone;
        spec.minValue = d.min;
        spec.maxValue = d.max;
        switch (d.kind) {
            case ParamKind::Float: spec.type = ParamType::Float; spec.defaultValue = ParamValue::scalar(d.def[0]); break;
            case ParamKind::Int: spec.type = ParamType::Int; spec.defaultValue = ParamValue::scalar(d.def[0]); break;
            case ParamKind::Bool: spec.type = ParamType::Bool; spec.defaultValue = ParamValue::boolean(d.def[0] >= 0.5f);
                spec.minValue = 0.0f; spec.maxValue = 1.0f; break;
            case ParamKind::Enum:
                spec.type = ParamType::Enum;
                spec.defaultValue = ParamValue::scalar(d.def[0]);
                spec.enumCount = static_cast<u32>(s.optionPtrs.size());
                spec.enumLabels = s.optionPtrs.data();
                break;
            case ParamKind::Color:
                spec.type = ParamType::Color;
                spec.defaultValue = ParamValue::color(d.def[0], d.def[1], d.def[2], d.def[3]);
                spec.minValue = 0.0f; spec.maxValue = 1.0f;
                break;
            case ParamKind::Point:
                spec.type = ParamType::Point2D;
                spec.flags |= kParamRelative;
                spec.defaultValue = ParamValue::vec2(d.def[0], d.def[1]);
                break;
            case ParamKind::Angle:
                spec.type = ParamType::Angle;
                spec.defaultValue = ParamValue::scalar(d.def[0]);
                spec.unit = "°";
                break;
        }
        (void)p.add(spec);
    }
}

std::vector<ParamStrings> make_strings(const Manifest& m) {
    std::vector<ParamStrings> out(m.params.size());
    for (usize i = 0; i < m.params.size(); ++i) {
        out[i].label = localized(m.params[i].label, "pt-BR");
        for (const LocalizedText& o : m.params[i].options) out[i].options.push_back(localized(o, "pt-BR"));
    }
    // Ponteiros só depois de todas as strings no lugar (o vetor não cresce mais).
    for (ParamStrings& s : out) for (const std::string& o : s.options) s.optionPtrs.push_back(o.c_str());
    return out;
}

// =============================================================================
// Plugin "shader"
// =============================================================================
class PluginShaderEffect final : public Effect {
public:
    PluginShaderEffect(const Manifest& m, std::vector<ShaderId> shaders)
        : manifest_(m), shaders_(std::move(shaders)) {
        key_ = effect_key(m.id);
        name_ = localized(m.name, "pt-BR");
        strings_ = make_strings(manifest_);
        info_ = EffectInfo{key_.c_str(), name_.c_str(), kCatalogCategory, EffectClass::Neighborhood};
    }
    const EffectInfo& info() const noexcept override { return info_; }
    void declare_parameters(ParameterRegistry& p) const override {
        declare_from_manifest(manifest_, strings_, p);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_plugin_prepare_frag, work));
        for (const ShaderId id : shaders_) out.push_back(PipelineKey::fullscreen(id, work));
    }
    f32 input_margin(const EffectEval&) const noexcept override { return manifest_.margin; }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        if (!input.valid() || shaders_.size() != manifest_.passes.size()) return Errc::InvalidArgument;
        const bool srgb = manifest_.colorSpace == ColorSpace::Srgb;
        // Região de trabalho: a entrada, ou ela espalhada pela margem do plugin
        // (recortada ao que o quadro mostra, mais o que os seguintes leem).
        const Rect region = manifest_.margin > 0.0f
            ? builtin::spread_region(input.region, manifest_.margin, manifest_.margin, e.placement, margin)
            : input.region;
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);
        const bool sameGrid = region.x == input.region.x && region.y == input.region.y && region.w == input.region.w
                           && region.h == input.region.h && w == input.width && h == input.height;
        FGTexture src = input.texture;
        if (!sameGrid || srgb) {
            struct { Vec4 uvMap; Vec4 mode; } prep{};
            prep.uvMap = EffectBuildContext::uv_map(region, input.region);
            prep.mode = Vec4{srgb ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
            src = ctx.texture("plugin-entrada", w, h);
            if (ctx.fullscreen_pass("plugin-entrada", PassStage::Effects, src, ShaderId::effects_plugin_prepare_frag,
                                    {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                    &prep, sizeof(prep)) == kInvalidIndex) {
                return Errc::PipelineCompileFailed;
            }
        }

        PluginUniforms u{};
        const f64 fps = e.framesPerSecond > 0.0 ? e.framesPerSecond : 30.0;
        const f64 frame = e.time_frames();
        u.time = Vec4{static_cast<f32>(frame / fps), static_cast<f32>(frame), static_cast<f32>(fps), 0.0f};
        const f32 layerW = e.placement && e.placement->layerWidth ? static_cast<f32>(e.placement->layerWidth) : region.w;
        const f32 layerH = e.placement && e.placement->layerHeight ? static_cast<f32>(e.placement->layerHeight) : region.h;
        u.region = Vec4{region.x, region.y, region.w, region.h};
        for (u32 i = 0; i < manifest_.params.size() && i < e.count; ++i) {
            const ParamValue& v = e.values[i];
            Vec4& slot = u.params[i];
            switch (manifest_.params[i].kind) {
                case ParamKind::Angle: slot = Vec4{v.v[0] * kDegToRad, 0, 0, 0}; break;
                case ParamKind::Color: {
                    slot = Vec4{v.v[0], v.v[1], v.v[2], v.v[3]};
                    if (srgb) slot = Vec4{linear_to_srgb(slot.x), linear_to_srgb(slot.y), linear_to_srgb(slot.z), slot.w};
                    break;
                }
                case ParamKind::Point: {
                    // Relativo à camada → uv da região de trabalho.
                    const f32 px = v.v[0] * layerW, py = v.v[1] * layerH;
                    slot = Vec4{region.w > 0 ? (px - region.x) / region.w : 0.5f,
                                region.h > 0 ? (py - region.y) / region.h : 0.5f, 0, 0};
                    break;
                }
                default: slot = Vec4{v.v[0], 0, 0, 0}; break;
            }
        }

        FGTexture targets[kMaxPasses]{};
        u32 lastW = w, lastH = h;
        FGTexture result{};
        for (u32 p = 0; p < manifest_.passes.size(); ++p) {
            const PassDecl& pass = manifest_.passes[p];
            const u32 pw = std::max(1u, static_cast<u32>(std::lround(static_cast<f32>(w) * pass.scale)));
            const u32 ph = std::max(1u, static_cast<u32>(std::lround(static_cast<f32>(h) * pass.scale)));
            u.resolution = Vec4{static_cast<f32>(pw), static_cast<f32>(ph), 1.0f / static_cast<f32>(pw), 1.0f / static_cast<f32>(ph)};
            u.layer = Vec4{layerW, layerH, region.w > 0 ? static_cast<f32>(pw) / region.w : 1.0f,
                           region.h > 0 ? static_cast<f32>(ph) / region.h : 1.0f};
            const bool last = p + 1 == manifest_.passes.size();
            const FGTexture dst = ctx.texture(last ? "plugin-saida" : kPassNames[p], pw, ph);
            PassTexture t[1 + kMaxPassInputs]{};
            u32 n = 0;
            t[n++] = PassTexture{src, {}, CommonSampler::LinearBorder};
            for (const auto& [name, from] : pass.inputs) {
                (void)name;
                FGTexture tex = src;
                if (from != "source") {
                    for (u32 q = 0; q < p; ++q)
                        if (manifest_.passes[q].target == from) tex = targets[q];
                }
                t[n++] = PassTexture{tex, {}, CommonSampler::LinearBorder};
            }
            u32 added = kInvalidIndex;
            switch (n) {
                case 1: added = ctx.fullscreen_pass(kPassNames[p], PassStage::Effects, dst, shaders_[p], {t[0]}, &u, sizeof(u)); break;
                case 2: added = ctx.fullscreen_pass(kPassNames[p], PassStage::Effects, dst, shaders_[p], {t[0], t[1]}, &u, sizeof(u)); break;
                case 3: added = ctx.fullscreen_pass(kPassNames[p], PassStage::Effects, dst, shaders_[p], {t[0], t[1], t[2]}, &u, sizeof(u)); break;
                default: added = ctx.fullscreen_pass(kPassNames[p], PassStage::Effects, dst, shaders_[p], {t[0], t[1], t[2], t[3]}, &u, sizeof(u)); break;
            }
            if (added == kInvalidIndex) return Errc::PipelineCompileFailed;
            targets[p] = dst;
            result = dst;
            lastW = pw;
            lastH = ph;
        }
        out = input;
        out.texture = result;
        out.region = region;
        out.width = lastW;
        out.height = lastH;
        out.history = FGTexture{};
        return OkStatus;
    }

private:
    Manifest manifest_;
    std::vector<ShaderId> shaders_;
    std::string key_, name_;
    std::vector<ParamStrings> strings_;
    EffectInfo info_{};
};

// =============================================================================
// Plugin "stack"
// =============================================================================
class PluginStackEffect final : public Effect {
public:
    PluginStackEffect(const Manifest& m, std::vector<CompiledStackStep> steps)
        : manifest_(m), steps_(std::move(steps)) {
        key_ = effect_key(m.id);
        name_ = localized(m.name, "pt-BR");
        strings_ = make_strings(manifest_);
        info_ = EffectInfo{key_.c_str(), name_.c_str(), kCatalogCategory, EffectClass::Neighborhood};
    }
    const EffectInfo& info() const noexcept override { return info_; }
    void declare_parameters(ParameterRegistry& p) const override {
        declare_from_manifest(manifest_, strings_, p);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        for (const CompiledStackStep& s : steps_) if (s.effect) s.effect->pipelines(out, work);
    }
    [[nodiscard]] bool expands_stack() const noexcept override { return true; }
    bool expand_stack(const ParamValue* values, u32 count, std::vector<StackStep>& out) const override {
        out.clear();
        for (const CompiledStackStep& s : steps_) {
            StackStep step;
            step.type = s.type;
            for (const CompiledStackValue& v : s.values) step.values.emplace_back(v.param, resolve(v, values, count));
            out.push_back(std::move(step));
        }
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        std::vector<ParamValue> scratch;
        for (const CompiledStackStep& s : steps_) {
            const EffectEval sub = sub_eval(s, e, scratch);
            if (!s.effect->is_identity(sub)) return false;
        }
        return true;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        std::vector<ParamValue> scratch;
        f32 sum = 0.0f;
        for (const CompiledStackStep& s : steps_) {
            const EffectEval sub = sub_eval(s, e, scratch);
            sum += s.effect->input_margin(sub);
        }
        return sum;
    }

    // Só a prévia do catálogo chega aqui (sem planejamento): os embutidos em
    // sequência, cada um com os valores do instante.
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        LayerImage cur = input;
        std::vector<std::vector<ParamValue>> values(steps_.size());
        std::vector<EffectEval> evals(steps_.size());
        for (usize k = 0; k < steps_.size(); ++k) evals[k] = sub_eval(steps_[k], e, values[k]);
        for (usize k = 0; k < steps_.size(); ++k) {
            const CompiledStackStep& s = steps_[k];
            EffectEval sub = evals[k];
            sub.values = values[k].data();
            if (s.effect->is_identity(sub)) continue;
            f32 after = margin;
            for (usize j = k + 1; j < steps_.size(); ++j) {
                EffectEval later = evals[j];
                later.values = values[j].data();
                after += steps_[j].effect->input_margin(later);
            }
            if (s.effect->effect_class() == EffectClass::PerPixel) {
                ColorOp op{};
                if (!s.effect->color_op(sub, op)) continue;
                ColorStackUniforms cu{};
                cu.header[0] = 1.0f;
                cu.header[1] = cur.width > 0 ? 1.0f / static_cast<f32>(cur.width) : 0.0f;
                cu.header[2] = cur.height > 0 ? 1.0f / static_cast<f32>(cur.height) : 0.0f;
                cu.header[3] = cur.texel_scale_x();
                std::memcpy(cu.ops, op.p, sizeof(op.p));
                cu.ops[0] = static_cast<f32>(static_cast<u32>(op.code));
                LayerImage next = cur;
                next.texture = ctx.texture("plugin-cor", cur.width, cur.height);
                if (ctx.fullscreen_pass("plugin-cor", PassStage::Effects, next.texture, ShaderId::effects_color_stack_frag,
                                        {PassTexture{cur.texture, {}, CommonSampler::NearestClamp},
                                         PassTexture{{}, op.lut, CommonSampler::LinearClamp}},
                                        &cu, sizeof(cu)) == kInvalidIndex) {
                    return Errc::PipelineCompileFailed;
                }
                cur = next;
                continue;
            }
            LayerImage next;
            const Status st = s.effect->build(ctx, sub, cur, after, next);
            if (st.ok() && next.valid()) cur = next;
        }
        out = cur;
        return OkStatus;
    }

private:
    static ParamValue resolve(const CompiledStackValue& v, const ParamValue* values, u32 count) noexcept {
        ParamValue out{};
        if (v.copySlot >= 0) {
            if (static_cast<u32>(v.copySlot) < count) out = values[v.copySlot];
            out.ref = 0;
        } else {
            for (u32 c = 0; c < v.components && c < 4; ++c) out.v[c] = v.expr[c].evaluate(values, count);
            if (v.type == ParamType::Color && v.components == 3) out.v[3] = 1.0f;
        }
        switch (v.type) {
            case ParamType::Bool: out = ParamValue::boolean(out.v[0] >= 0.5f); break;
            case ParamType::Int: case ParamType::Enum: out.v[0] = std::round(out.v[0]); break;
            default: break;
        }
        return out;
    }
    /// Os valores do embutido (padrões + o que a pilha define), presos à faixa
    /// digitada dele — o que `evaluate_param` faria no planejamento.
    EffectEval sub_eval(const CompiledStackStep& s, const EffectEval& e, std::vector<ParamValue>& values) const {
        values.assign(s.params->count(), ParamValue{});
        for (u32 i = 0; i < s.params->count(); ++i) values[i] = s.params->at(i).defaultValue;
        for (const CompiledStackValue& v : s.values) {
            if (v.param >= values.size()) continue;
            ParamValue pv = resolve(v, e.values, e.count);
            const ParamSpec& spec = s.params->at(v.param);
            for (u32 c = 0; c < component_count(spec.type) && c < 4; ++c)
                pv.v[c] = std::clamp(pv.v[c], spec.typed_min(), spec.typed_max());
            values[v.param] = pv;
        }
        EffectEval sub = e;
        sub.effect = s.effect;
        sub.values = values.data();
        sub.valueOffset = 0;
        sub.count = s.params->count();
        return sub;
    }

    Manifest manifest_;
    std::vector<CompiledStackStep> steps_;
    std::string key_, name_;
    std::vector<ParamStrings> strings_;
    EffectInfo info_{};
};

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

/// Embutidos que vivem fora da cadeia de pixels (o renderer os lê direto da
/// camada) ou que dependem de dado da instância: numa pilha não fariam nada,
/// ou fariam errado.
bool stackable(std::string_view key) noexcept {
    for (const char* prefix : {"aurea.time.", "aurea.audio.", "aurea.control.", "aurea.3d.", "aurea.layout.",
                               "aurea.text", "aurea.shape3d.", "plugin."})
        if (key.starts_with(prefix)) return false;
    for (const char* exact : {kMeshWarpKey, puppet::kPuppetKey, "aurea.transform.parenting_helper",
                              "aurea.generate.particular", "aurea.key.rotobrush", text::kAnimatorEffect})
        if (key == exact) return false;
    return true;
}

} // namespace

bool compile_stack(const Manifest& m, const EffectRegistry& registry, std::vector<CompiledStackStep>& out,
                   std::string& error) {
    out.clear();
    error.clear();
    std::vector<ExprSymbol> symbols;
    for (u32 i = 0; i < m.params.size(); ++i) {
        const ParamDecl& p = m.params[i];
        if (p.kind == ParamKind::Point) {
            symbols.push_back({p.id + ".x", i, 0});
            symbols.push_back({p.id + ".y", i, 1});
        } else if (p.kind == ParamKind::Color) {
            const char* names[] = {"r", "g", "b", "a", "x", "y", "z", "w"};
            for (u32 c = 0; c < 8; ++c) symbols.push_back({p.id + "." + names[c], i, c % 4});
        } else {
            symbols.push_back({p.id, i, 0});
        }
    }
    for (usize k = 0; k < m.stack.size(); ++k) {
        const StackStepDecl& decl = m.stack[k];
        const std::string where = "stack[" + std::to_string(k) + "] (" + decl.effect + ")";
        if (!stackable(decl.effect))
            return (error = "plugin.json: " + where + ": este efeito nao pode entrar numa pilha "
                            "(depende da camada, do tempo ou de dado proprio)", false);
        const EffectTypeId type = registry.find_key(decl.effect);
        const Effect* effect = type ? registry.find(type) : nullptr;
        const ParameterRegistry* params = type ? registry.params(type) : nullptr;
        if (!effect || !params)
            return (error = "plugin.json: " + where + ": efeito embutido desconhecido", false);
        CompiledStackStep step;
        step.type = type;
        step.effect = effect;
        step.params = params;
        for (const StackValue& sv : decl.set) {
            const u32 index = params->find(sv.param);
            if (index == kInvalidIndex)
                return (error = "plugin.json: " + where + ": parametro desconhecido \"" + sv.param + "\"", false);
            const ParamSpec& spec = params->at(index);
            const u32 comps = component_count(spec.type);
            if (comps == 0)
                return (error = "plugin.json: " + where + ": \"" + sv.param + "\" (curva, degrade ou referencia) "
                                "nao pode ser definido por plugin", false);
            CompiledStackValue cv;
            cv.param = index;
            cv.type = spec.type;
            cv.components = comps;
            if (comps > 1 && sv.components.size() == 1) {
                const std::string name = trim(sv.components[0]);
                bool copied = false;
                for (u32 i = 0; i < m.params.size(); ++i) {
                    const ParamDecl& p = m.params[i];
                    const bool fits = (spec.type == ParamType::Color && p.kind == ParamKind::Color)
                                   || ((spec.type == ParamType::Point2D || spec.type == ParamType::Point3D) && p.kind == ParamKind::Point);
                    if (p.id == name && fits) { cv.copySlot = static_cast<i32>(i); copied = true; break; }
                }
                if (!copied)
                    return (error = "plugin.json: " + where + ": \"" + sv.param + "\" tem " + std::to_string(comps)
                                    + " componentes: use uma lista ou o id de um parametro do mesmo tipo", false);
            } else {
                const bool colorRgb = spec.type == ParamType::Color && sv.components.size() == 3;
                if (sv.components.size() != comps && !colorRgb)
                    return (error = "plugin.json: " + where + ": \"" + sv.param + "\" precisa de " + std::to_string(comps)
                                    + " valor(es)", false);
                cv.components = static_cast<u32>(sv.components.size());
                for (usize c = 0; c < sv.components.size(); ++c) {
                    std::string why;
                    if (!cv.expr[c].compile(sv.components[c], symbols, why))
                        return (error = "plugin.json: " + where + ": \"" + sv.param + "\": " + why, false);
                }
            }
            step.values.push_back(std::move(cv));
        }
        out.push_back(std::move(step));
    }
    return true;
}

std::unique_ptr<Effect> make_shader_effect(const Manifest& m, std::vector<ShaderId> passShaders) {
    return std::make_unique<PluginShaderEffect>(m, std::move(passShaders));
}

std::unique_ptr<Effect> make_stack_effect(const Manifest& m, std::vector<CompiledStackStep> steps) {
    return std::make_unique<PluginStackEffect>(m, std::move(steps));
}

} // namespace aurea::plugins
