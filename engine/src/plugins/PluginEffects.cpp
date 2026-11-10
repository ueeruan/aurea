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
#include "aurea/timeline/Layer.hpp"
#include "effects/builtin/BuiltinEffects.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/plugins/Script.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <string>
#include <cctype>
#include <cmath>
#include <cstring>

namespace aurea::plugins {

namespace {

constexpr f32 kDegToRad = 3.14159265358979323846f / 180.0f;

// Nomes estáticos: as medições de GPU por passe guardam o ponteiro.
const char* pass_name(u32 p) noexcept {
    static const auto names = [] {
        std::array<std::string, kMaxPasses> n;
        for (u32 i = 0; i < kMaxPasses; ++i) n[i] = "plugin-passe-" + std::to_string(i + 1);
        return n;
    }();
    return names[std::min(p, kMaxPasses - 1)].c_str();
}

/// O bloco `aurea` do cabeçalho (std140). Na GPU vai EMPACOTADO: cabeçalho,
/// só os `param_slots` vec4 que o plugin declara e, no 3D, a cena logo depois
/// (o cabeçalho GLSL declara `params[param_slots]`). Ver `pack_uniforms`.
struct PluginUniforms {
    Vec4 resolution;
    Vec4 time;
    Vec4 layer;
    Vec4 region;
    Vec4 params[kMaxShaderParams];
};
constexpr usize kUniformHeaderBytes = 4 * sizeof(Vec4);
static_assert(sizeof(PluginUniforms) == kUniformHeaderBytes + kMaxShaderParams * sizeof(Vec4), "layout std140 do bloco aurea");
static_assert(sizeof(PluginUniforms) <= binding::kMaxUniformBytes);

/// 0.3.1, plugins "shader3d": o mesmo bloco com a cena acrescentada no FIM
/// (os plugins "shader" seguem recebendo só os 320 bytes de antes). Mat4 é
/// coluna a coluna, como o mat4 do std140. Ver PluginShader.cpp.
struct PluginUniforms3D {
    PluginUniforms base;
    Mat4 view;            ///< vista <- mundo (x direita, y baixo, z frente)
    Mat4 compFromWorld;   ///< px da composição (homogêneo) <- mundo
    Mat4 compFromLayer;   ///< px da composição (homogêneo) <- px da camada
    Vec4 camera;          ///< xyz posição no mundo, w = 1 câmera de camada / 0 padrão
    Vec4 lens;            ///< x fovY (rad), y plano próximo, z aspecto, w focal em px
    Vec4 comp;            ///< xy tamanho da composição (px), zw = 1/xy
    Mat4 layerWorld[kMaxLayerParams];  ///< mundo <- px de cada camada ligada
    Vec4 layerPos[kMaxLayerParams];    ///< xyz âncora no mundo, w = 1 ligada e visível
    Vec4 layerInfo[kMaxLayerParams];   ///< x tipo (6 nulo, 8 câmera, 9 luz), y fovY (câmera), z 3D
};
constexpr usize kUniformSceneBytes = sizeof(PluginUniforms3D) - sizeof(PluginUniforms);
static_assert(kUniformSceneBytes == 3 * 64 + 3 * 16 + kMaxLayerParams * (64 + 16 + 16), "layout std140 da cena dos plugins 3D");
static_assert(kUniformHeaderBytes + kMaxShader3dParams * sizeof(Vec4) + kUniformSceneBytes <= binding::kMaxUniformBytes);

/// O bloco como o shader o declara: cabeçalho, `slots` parâmetros e a cena (3D).
u32 pack_uniforms(const PluginUniforms3D& u, u32 slots, bool scene3d, u8* out) noexcept {
    // O manifesto recusa acima disto; aqui é a última linha antes de escrever fora do bloco.
    const usize params = static_cast<usize>(std::min(slots, scene3d ? kMaxShader3dParams : kMaxShaderParams)) * sizeof(Vec4);
    std::memcpy(out, &u.base, kUniformHeaderBytes + params);
    if (!scene3d) return static_cast<u32>(kUniformHeaderBytes + params);
    std::memcpy(out + kUniformHeaderBytes + params, &u.view, kUniformSceneBytes);
    return static_cast<u32>(kUniformHeaderBytes + params + kUniformSceneBytes);
}

CommonSampler texture_sampler(const TextureDecl& t) noexcept {
    if (t.filter == 1) return CommonSampler::NearestClamp;
    return t.wrap == 1 ? CommonSampler::LinearRepeat : t.wrap == 2 ? CommonSampler::LinearMirror : CommonSampler::LinearClamp;
}
/// Vec4 por camada ligada em EffectEval::pathSamples: 4 colunas, posição, info.
constexpr u32 kLayerWorldStride = 6;

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
            case ParamKind::Curve: spec.type = ParamType::Curve; spec.flags = kParamNone; break;
            case ParamKind::Gradient: spec.type = ParamType::Gradient; spec.flags = kParamNone; break;
            case ParamKind::LayerImage:
                spec.type = ParamType::LayerReference;
                spec.flags = kParamNone;
                spec.defaultValue = ParamValue::scalar(-1.0f);
                spec.minValue = -1.0f; spec.maxValue = 1.0e6f;
                break;
            case ParamKind::Layer:
                // A mesma referência de camada dos embutidos (reabrir, presets,
                // copiar e colar já a remapeiam); as telas filtram pelo tipo.
                spec.type = ParamType::LayerReference;
                spec.flags = static_cast<u16>(((d.accept & kAcceptCamera) ? kParamRefCamera : 0)
                                            | ((d.accept & kAcceptNull) ? kParamRefNull : 0)
                                            | ((d.accept & kAcceptLight) ? kParamRefLight : 0));
                spec.defaultValue = ParamValue::scalar(-1.0f);
                spec.minValue = -1.0f; spec.maxValue = 1.0e6f;
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

/// `onFrame(f)` do plugin: devolve valores novos para os parâmetros neste quadro.
/// Roda no planejamento, sob o lock do modelo: passos, tempo e memória curtos, e
/// três falhas seguidas o desligam até reabrir o app. O efeito segue sem ele.
class FrameScript {
public:
    FrameScript(const std::string& source, const std::string& id) : id_(id) {
        script::Limits limits;
        limits.maxSteps = kFrameScriptMaxSteps;
        limits.maxTimeMs = kFrameScriptMaxMs;
        limits.maxHeapBytes = kFrameScriptMaxHeap;
        limits.maxDepth = 64;
        ctx_ = std::make_unique<script::Context>(limits);
        script::ScriptError error;
        ready_ = ctx_->run(source, "frameScript", error) && ctx_->has_function("onFrame");
        if (!ready_) AUREA_LOG_WARN("plugin '%s': frameScript sem onFrame ou com erro: %s", id_.c_str(), error.text().c_str());
    }

    /// `out`: dois Vec4 por parâmetro (valor; x = 1 se o script mudou). Falso = sem mudança.
    bool eval(const Manifest& m, const EffectEval& e, std::vector<Vec4>& out) noexcept {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock() || !ready_ || failures_ >= kFrameScriptMaxFailures) return false;
        const usize base = out.size();
        const f64 frame = e.time_frames(), fps = e.framesPerSecond > 0.0 ? e.framesPerSecond : 30.0;
        script::ScriptError error;
        bool ok = false;
        try {
            using script::Value;
            // Mesmo quadro, mesmo Math.random: prévia e exportação batem.
            ctx_->seed(0x9E3779B97F4A7C15ull ^ static_cast<u64>(std::llround(frame * 1000.0)));
            const Value arg = Value::object(ctx_->new_object());
            ctx_->set(arg, "time", Value::number(frame / fps));
            ctx_->set(arg, "frame", Value::number(frame));
            ctx_->set(arg, "fps", Value::number(fps));
            ctx_->set(arg, "progress", Value::number(e.clipProgress));
            const Value params = Value::object(ctx_->new_object());
            for (u32 i = 0; i < m.params.size() && i < e.count; ++i) {
                const ParamKind k = m.params[i].kind;
                if (k == ParamKind::Curve || k == ParamKind::Gradient || k == ParamKind::Layer || k == ParamKind::LayerImage) continue;
                ctx_->set(params, m.params[i].id, Value::number(e.values[i].v[0]));
            }
            ctx_->set(arg, "params", params);
            Value result;
            ok = ctx_->call("onFrame", {arg}, result, error);
            if (ok && script::Context::type_name(result) == "object") {
                out.resize(base + m.params.size() * 2, Vec4{});
                for (u32 i = 0; i < m.params.size() && i < e.count; ++i) {
                    const Value v = ctx_->get(result, m.params[i].id);
                    const std::string type = script::Context::type_name(v);
                    Vec4 value{e.values[i].v[0], e.values[i].v[1], e.values[i].v[2], e.values[i].v[3]};
                    if (type == "number") value.x = finite(ctx_->to_number(v), value.x);
                    else if (type == "object") {
                        f32* parts[4] = {&value.x, &value.y, &value.z, &value.w};
                        for (u32 c = 0; c < 4; ++c) {
                            const Value part = ctx_->get(v, std::to_string(c));
                            if (script::Context::type_name(part) == "number") *parts[c] = finite(ctx_->to_number(part), *parts[c]);
                        }
                    } else continue;
                    out[base + i * 2] = value;
                    out[base + i * 2 + 1] = Vec4{1.0f, 0.0f, 0.0f, 0.0f};
                }
            }
            if ((++calls_ & 255u) == 0u) (void)ctx_->collect();
        } catch (...) {
            ok = false;
        }
        if (ok) { failures_ = 0; return out.size() > base; }
        out.resize(base);
        if (++failures_ == kFrameScriptMaxFailures)
            AUREA_LOG_WARN("plugin '%s': frameScript desligado depois de %u falhas: %s", id_.c_str(), kFrameScriptMaxFailures, error.text().c_str());
        return false;
    }

private:
    static f32 finite(f64 v, f32 fallback) noexcept { return std::isfinite(v) ? static_cast<f32>(v) : fallback; }
    std::string id_;
    std::unique_ptr<script::Context> ctx_;
    std::mutex mutex_;
    bool ready_ = false;
    u32 failures_ = 0;
    u32 calls_ = 0;
};

// =============================================================================
// Plugin "shader"
// =============================================================================
class PluginShaderEffect final : public Effect {
public:
    PluginShaderEffect(const Manifest& m, std::vector<ShaderId> shaders, PluginImages images, const std::string& frameSource)
        : manifest_(m), shaders_(std::move(shaders)), images_(std::move(images)) {
        if (!frameSource.empty()) frameScript_ = std::make_unique<FrameScript>(frameSource, m.id);
        for (u32 i = 0; i < manifest_.params.size(); ++i)
            if (manifest_.params[i].kind == ParamKind::LayerImage) imageParams_.push_back(i);
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
    bool wants_history() const noexcept override { return manifest_.history; }
    u32 input_layer_count() const noexcept override { return static_cast<u32>(imageParams_.size()); }
    i32 input_layer_param_at(u32 slot) const noexcept override { return slot < imageParams_.size() ? static_cast<i32>(imageParams_[slot]) : -1; }

    /// "shader3d": o mundo de cada camada ligada, lido sob o lock do modelo e
    /// levado até a montagem em `pathSamples` (kLayerWorldStride por camada).
    void resolve_resources(EffectEval& e) const noexcept override {
        if (!e.resources) return;
        // 0.4.6: as imagens do pacote, na GPU uma vez (cache do renderer por conteúdo).
        for (usize i = 0; i < images_.size() && i < kEffectAuxImages; ++i) {
            const PluginImage* img = images_[i].get();
            if (img && !img->rgba.empty())
                e.auxImages[i] = e.resources->image_texture(img->key, img->rgba.data(), img->width, img->height, img->srgb);
        }
        if (manifest_.audio && e.layer) {
            AudioAnalysisRequest req;
            req.host = e.layer;
            req.instance = e.instance;
            req.spectrum = true;
            req.count = 64;
            req.durationMs = 200.0f;
            req.startHz = 20.0f;
            req.endHz = 12000.0f;
            const AudioAnalysisResult r = e.resources->audio_analysis(req);
            e.aux = r.texture;
            e.auxInfo = Vec4{r.texture.valid() ? 1.0f : 0.0f, r.peak, 0.0f, 0.0f};
        }
        auto data = std::make_shared<std::vector<Vec4>>();
        // Ordem em `pathSamples`: mundo das camadas, curvas e degradês, script por quadro.
        if (manifest_.scene3d && e.layer) resolve_layers(e, *data);
        bake_tables(e, *data);
        if (frameScript_) (void)frameScript_->eval(manifest_, e, *data);
        if (!data->empty()) e.pathSamples = std::move(data);
    }

    void resolve_layers(EffectEval& e, std::vector<Vec4>& out) const noexcept {
        auto* data = &out;
        data->reserve(kMaxLayerParams * kLayerWorldStride);
        const f64 compTime = e.time_frames() + static_cast<f64>(e.layer->start.value) - static_cast<f64>(e.layer->offset.value);
        for (u32 i = 0; i < manifest_.params.size() && i < e.count; ++i) {
            if (manifest_.params[i].kind != ParamKind::Layer) continue;
            if (data->size() >= kMaxLayerParams * kLayerWorldStride) break;
            LayerWorldInfo w;
            const bool ok = e.resources->layer_world(e.layer, e.values[i].ref, compTime, w);
            for (u32 c = 0; c < 4; ++c) data->push_back(ok ? w.worldFromLayer.col[c] : Mat4::identity().col[c]);
            data->push_back(Vec4{w.position.x, w.position.y, w.position.z, ok ? 1.0f : 0.0f});
            data->push_back(Vec4{static_cast<f32>(w.kind), w.fovY, w.threeD ? 1.0f : 0.0f, 0.0f});
        }
    }

    /// Curvas e degradês viram números: só existem na instância durante o planejamento.
    void bake_tables(const EffectEval& e, std::vector<Vec4>& out) const noexcept {
        const bool srgb = manifest_.colorSpace == ColorSpace::Srgb;
        for (u32 i = 0; i < manifest_.params.size() && i < e.count; ++i) {
            const ParamKind kind = manifest_.params[i].kind;
            if (kind == ParamKind::Curve) {
                const CurveData* curve = e.curve(i);
                for (u32 slot = 0; slot < kCurveSlots; ++slot) {
                    f32 v[4];
                    for (u32 c = 0; c < 4; ++c) {
                        const f32 t = static_cast<f32>(slot * 4 + c) / static_cast<f32>(kCurveSamples - 1);
                        v[c] = curve ? curve->evaluate(0, t) : t;
                    }
                    out.push_back(Vec4{v[0], v[1], v[2], v[3]});
                }
            } else if (kind == ParamKind::Gradient) {
                const u64 ref = e.values[i].ref;
                const GradientData* g = e.instance && ref < e.instance->gradients.size() ? &e.instance->gradients[ref] : nullptr;
                for (u32 slot = 0; slot < kGradientSlots; ++slot) {
                    Vec4 color = sample_gradient(g, static_cast<f32>(slot) / static_cast<f32>(kGradientSlots - 1));
                    if (srgb) color = Vec4{linear_to_srgb(color.x), linear_to_srgb(color.y), linear_to_srgb(color.z), color.w};
                    out.push_back(color);
                }
            }
        }
    }

    static Vec4 sample_gradient(const GradientData* g, f32 t) noexcept {
        if (!g || g->stops.empty()) return Vec4{t, t, t, 1.0f};
        const auto& stops = g->stops;
        if (t <= stops.front().position) return stops.front().color;
        for (usize k = 1; k < stops.size(); ++k) {
            if (t > stops[k].position) continue;
            const GradientStop& a = stops[k - 1], &b = stops[k];
            const f32 span = std::max(b.position - a.position, 1e-5f), f = (t - a.position) / span;
            return Vec4{a.color.x + (b.color.x - a.color.x) * f, a.color.y + (b.color.y - a.color.y) * f,
                        a.color.z + (b.color.z - a.color.z) * f, a.color.w + (b.color.w - a.color.w) * f};
        }
        return stops.back().color;
    }

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

        PluginUniforms3D u3{};
        PluginUniforms& u = u3.base;
        const u32 slots = param_slots(manifest_);
        alignas(16) u8 packed[binding::kMaxUniformBytes];
        const f64 fps = e.framesPerSecond > 0.0 ? e.framesPerSecond : 30.0;
        const f64 frame = e.time_frames();
        u.time = Vec4{static_cast<f32>(frame / fps), static_cast<f32>(frame), static_cast<f32>(fps), 0.0f};
        const f32 layerW = e.placement && e.placement->layerWidth ? static_cast<f32>(e.placement->layerWidth) : region.w;
        const f32 layerH = e.placement && e.placement->layerHeight ? static_cast<f32>(e.placement->layerHeight) : region.h;
        u.region = Vec4{region.x, region.y, region.w, region.h};
        // Depois do mundo das camadas: as tabelas; depois delas, o script por quadro.
        const std::vector<Vec4>* samples = e.pathSamples.get();
        usize cursor = manifest_.scene3d ? scene_layer_count(e) * kLayerWorldStride : 0;
        usize tableBytes = 0;
        for (const ParamDecl& d : manifest_.params) tableBytes += d.kind == ParamKind::Curve ? kCurveSlots : d.kind == ParamKind::Gradient ? kGradientSlots : 0u;
        const usize overrides = cursor + tableBytes;
        const bool driven = samples && samples->size() >= overrides + manifest_.params.size() * 2 && !manifest_.params.empty();
        for (u32 i = 0; i < manifest_.params.size() && i < e.count; ++i) {
            const u32 at = param_slot_at(manifest_, i);
            if (at >= kMaxShaderParams) break;
            ParamValue v = e.values[i];
            if (driven && (*samples)[overrides + i * 2 + 1].x > 0.5f) {
                const Vec4 o = (*samples)[overrides + i * 2];
                v.v[0] = o.x; v.v[1] = o.y; v.v[2] = o.z; v.v[3] = o.w;
            }
            Vec4& slot = u.params[at];
            if (manifest_.params[i].kind == ParamKind::Curve || manifest_.params[i].kind == ParamKind::Gradient) {
                const u32 count = param_slot_size(manifest_.params[i]);
                for (u32 k = 0; k < count && samples && cursor + k < samples->size() && at + k < kMaxShaderParams; ++k) u.params[at + k] = (*samples)[cursor + k];
                cursor += count;
                continue;
            }
            if (manifest_.params[i].kind == ParamKind::LayerImage) {
                slot = Vec4{v.ref && ctx.layer_input(v.ref) ? 1.0f : 0.0f, 0, 0, 0};
                continue;
            }
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
                case ParamKind::Layer: break;   // abaixo (mundo da camada)
                default: slot = Vec4{v.v[0], 0, 0, 0}; break;
            }
        }
        if (manifest_.audio && slots > 0) u.params[slots - 1] = Vec4{e.auxInfo.y, e.auxInfo.x, 0.0f, 0.0f};
        if (manifest_.scene3d) fill_scene(e, u3);

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
            const FGTexture dst = ctx.texture(last ? "plugin-saida" : pass_name(p), pw, ph);
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
            // Imagens do pacote: sem GPU para ela (prévia sem renderer), o slot
            // recebe a fonte — nunca um descritor vazio.
            for (usize i = 0; i < manifest_.textures.size() && n < 1 + kMaxPassInputs; ++i) {
                const TextureHandle img = i < kEffectAuxImages ? e.auxImages[i] : TextureHandle{};
                t[n++] = img.valid() ? PassTexture{{}, img, texture_sampler(manifest_.textures[i])}
                                     : PassTexture{src, {}, CommonSampler::LinearClamp};
            }
            // Formato 3: sem o recurso, o slot recebe a fonte (nunca um descritor vazio).
            if (manifest_.history && n < 1 + kMaxPassInputs) {
                const LayerImage& past = ctx.history();
                t[n++] = past.valid() ? PassTexture{past.texture, {}, CommonSampler::LinearClamp} : PassTexture{src, {}, CommonSampler::LinearClamp};
            }
            if (manifest_.audio && n < 1 + kMaxPassInputs)
                t[n++] = e.aux.valid() ? PassTexture{{}, e.aux, CommonSampler::LinearClamp} : PassTexture{src, {}, CommonSampler::LinearClamp};
            for (const u32 index : imageParams_) {
                if (n >= 1 + kMaxPassInputs) break;
                const u64 ref = index < e.count ? e.values[index].ref : 0;
                const LayerImage* other = ref ? ctx.layer_input(ref) : nullptr;
                t[n++] = other && other->valid() ? PassTexture{other->texture, {}, CommonSampler::LinearClamp} : PassTexture{src, {}, CommonSampler::LinearClamp};
            }
            const u32 bytes = pack_uniforms(u3, slots, manifest_.scene3d, packed);
            const u32 added = ctx.fullscreen_pass(pass_name(p), PassStage::Effects, dst, shaders_[p], t, n, packed, bytes);
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
    /// A câmera da cena e as camadas ligadas no bloco 3D (sem câmera preenchida,
    /// a padrão da composição: no centro, a 0,5·h/tan(fov/2), olhando para +z).
    void fill_scene(const EffectEval& e, PluginUniforms3D& u) const noexcept {
        const LayerPlacement* pl = e.placement;
        const f32 cw = pl && pl->compWidth ? static_cast<f32>(pl->compWidth) : u.base.layer.x;
        const f32 ch = pl && pl->compHeight ? static_cast<f32>(pl->compHeight) : u.base.layer.y;
        u.comp = Vec4{cw, ch, 1.0f / std::max(1.0f, cw), 1.0f / std::max(1.0f, ch)};
        f32 fov = 40.0f * kDegToRad, nearZ = 1.0f;
        if (pl && pl->sceneCameraValid) {
            u.view = pl->sceneView;
            u.compFromWorld = pl->sceneCompFromWorld;
            u.camera = Vec4{pl->sceneCameraPosition.x, pl->sceneCameraPosition.y, pl->sceneCameraPosition.z,
                            pl->cameraActive ? 1.0f : 0.0f};
            fov = pl->sceneFovY > 0.0f ? pl->sceneFovY : fov;
            nearZ = pl->sceneNearZ;
        } else {
            // Sem a cena (prévia do catálogo, quadro de teste): câmera padrão.
            const f32 dist = 0.5f * ch / std::tan(0.5f * fov);
            u.view = Mat4::identity();
            u.view.col[3] = Vec4{-0.5f * cw, -0.5f * ch, dist, 1.0f};
            u.camera = Vec4{0.5f * cw, 0.5f * ch, -dist, 0.0f};
        }
        u.lens = Vec4{fov, nearZ, cw / std::max(1.0f, ch), 0.5f * ch / std::tan(0.5f * fov)};
        u.compFromLayer = pl ? pl->compFromLayer : Mat4::identity();
        for (u32 k = 0; k < kMaxLayerParams; ++k) u.layerWorld[k] = Mat4::identity();
        const std::vector<Vec4>* data = e.pathSamples.get();
        u32 slot = 0;
        for (u32 i = 0; i < manifest_.params.size() && i < e.count; ++i) {
            if (manifest_.params[i].kind != ParamKind::Layer) continue;
            if (slot >= kMaxLayerParams) break;
            const usize at = static_cast<usize>(slot) * kLayerWorldStride;
            if (data && data->size() >= at + kLayerWorldStride) {
                for (u32 c = 0; c < 4; ++c) u.layerWorld[slot].col[c] = (*data)[at + c];
                u.layerPos[slot] = (*data)[at + 4];
                u.layerInfo[slot] = (*data)[at + 5];
            }
            // params[i] da camada: (ligada, tipo, posição entre as camadas, 0).
            u.base.params[param_slot_at(manifest_, i)] = Vec4{u.layerPos[slot].w, u.layerInfo[slot].x, static_cast<f32>(slot), 0.0f};
            ++slot;
        }
    }

    [[nodiscard]] usize scene_layer_count(const EffectEval& e) const noexcept {
        usize n = 0;
        for (u32 i = 0; i < manifest_.params.size() && i < e.count; ++i) n += manifest_.params[i].kind == ParamKind::Layer;
        return std::min<usize>(n, kMaxLayerParams);
    }

    Manifest manifest_;
    std::vector<ShaderId> shaders_;
    PluginImages images_;
    std::unique_ptr<FrameScript> frameScript_;
    std::vector<u32> imageParams_;
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
                              "aurea.generate.particular", "aurea.key.rotobrush", "aurea.key.background_remove",
                              text::kAnimatorEffect,
                              "aurea.utility.copy_background"})
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

std::unique_ptr<Effect> make_shader_effect(const Manifest& m, std::vector<ShaderId> passShaders, PluginImages images, std::string frameSource) {
    return std::make_unique<PluginShaderEffect>(m, std::move(passShaders), std::move(images), frameSource);
}

std::unique_ptr<Effect> make_stack_effect(const Manifest& m, std::vector<CompiledStackStep> steps) {
    return std::make_unique<PluginStackEffect>(m, std::move(steps));
}

} // namespace aurea::plugins
