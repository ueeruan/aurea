#include "aurea/effects/EffectGraph.hpp"
#include "aurea/core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea {

// =============================================================================
// Utilidades de Effect.hpp
// =============================================================================
Rect visible_layer_rect(const LayerPlacement& pl) noexcept {
    // Parte 2D afim de comp←layer: x' = a x + c y + tx ; y' = b x + d y + ty.
    const Mat4& m = pl.compFromLayer;
    const f32 a = m.col[0].x, b = m.col[0].y;
    const f32 c = m.col[1].x, d = m.col[1].y;
    const f32 tx = m.col[3].x, ty = m.col[3].y;
    const f32 det = a * d - b * c;
    if (std::fabs(det) < 1e-12f || pl.compWidth == 0 || pl.compHeight == 0) return Rect{0, 0, 0, 0};
    const f32 inv = 1.0f / det;

    const f32 cw = static_cast<f32>(pl.compWidth);
    const f32 ch = static_cast<f32>(pl.compHeight);
    const f32 corners[4][2] = {{0, 0}, {cw, 0}, {0, ch}, {cw, ch}};
    f32 minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (const auto& q : corners) {
        const f32 qx = q[0] - tx, qy = q[1] - ty;
        const f32 lx = ( d * qx - c * qy) * inv;
        const f32 ly = (-b * qx + a * qy) * inv;
        minX = std::min(minX, lx); maxX = std::max(maxX, lx);
        minY = std::min(minY, ly); maxY = std::max(maxY, ly);
    }
    return Rect{minX, minY, maxX - minX, maxY - minY};
}

FGTexture EffectBuildContext::texture(const char* name, u32 width, u32 height) noexcept {
    TextureDesc d;
    d.width = std::max(1u, width);
    d.height = std::max(1u, height);
    d.format = workFormat_;
    d.sampled = true;
    d.renderTarget = true;
    return graph_.create_texture(name, d);
}

void EffectBuildContext::region_size(const Rect& region, f32 texelScale, u32& outW, u32& outH) const noexcept {
    f32 w = std::max(1.0f, std::ceil(region.w * texelScale - 1e-3f));
    f32 h = std::max(1.0f, std::ceil(region.h * texelScale - 1e-3f));
    const f32 limit = static_cast<f32>(maxTexture_);
    const f32 biggest = std::max(w, h);
    if (biggest > limit) {
        const f32 k = limit / biggest;
        w = std::max(1.0f, std::floor(w * k));
        h = std::max(1.0f, std::floor(h * k));
    }
    outW = static_cast<u32>(w);
    outH = static_cast<u32>(h);
}

Vec4 EffectBuildContext::uv_map(const Rect& out, const Rect& in) noexcept {
    const f32 iw = in.w > 0 ? in.w : 1.0f;
    const f32 ih = in.h > 0 ? in.h : 1.0f;
    return Vec4{out.w / iw, out.h / ih, (out.x - in.x) / iw, (out.y - in.y) / ih};
}

u32 EffectBuildContext::fullscreen_pass(const char* name, PassStage stage, FGTexture target,
                                        ShaderId fragment, std::initializer_list<PassTexture> textures,
                                        const void* uniforms, u32 uniformBytes, LoadOp load) noexcept {
    auto pipe = shaders_.pipeline(PipelineKey::fullscreen(fragment, graph_.desc(target).format));
    if (!pipe.ok()) return kInvalidIndex;

    void* u = nullptr;
    if (uniforms && uniformBytes) {
        u = arena_.alloc(uniformBytes, 16);
        if (!u) return kInvalidIndex;
        std::memcpy(u, uniforms, uniformBytes);
    }

    struct Bind { FGTexture graph; u64 raw; u64 sampler; };
    struct Capture {
        PipelineHandle pipeline;
        Bind binds[binding::kTextureSlots];
        u32 count;
        const void* uniforms;
        u32 uniformBytes;
    } cap{};
    cap.pipeline = *pipe;
    cap.uniforms = u;
    cap.uniformBytes = uniformBytes;
    for (const PassTexture& t : textures) {
        if (cap.count >= binding::kTextureSlots) break;
        cap.binds[cap.count++] = Bind{t.graph, t.raw.id, shaders_.sampler(t.sampler).id};
    }

    const u32 pass = graph_.add_raster_pass(name, stage, target, load, Vec4{0, 0, 0, 0},
        [cap](PassContext& pc) {
            pc.cmds.bind_pipeline(cap.pipeline);
            for (u32 i = 0; i < cap.count; ++i) {
                const Bind& b = cap.binds[i];
                const TextureHandle tex = b.graph.valid() ? pc.texture(b.graph) : TextureHandle{b.raw};
                if (tex.valid()) pc.cmds.bind_texture(i, tex, SamplerHandle{b.sampler});
            }
            if (cap.uniforms) pc.cmds.set_uniforms(cap.uniforms, cap.uniformBytes);
            pc.cmds.draw(3);
        });
    for (const PassTexture& t : textures) {
        if (t.graph.valid()) graph_.read(pass, t.graph);
    }
    return pass;
}

// =============================================================================
// EffectPlan
// =============================================================================
void EffectPlan::clear() noexcept {
    evals.clear();
    values.clear();
    colorOps.clear();
    stages.clear();
    blockers.clear();
    hasFold = false;
    foldMatrix = Mat4::identity();
    foldOpacity = 1.0f;
    droppedIdentity = 0;
    droppedUnknown = 0;
    fusedEffects = 0;
}

// =============================================================================
// Planejamento
// =============================================================================
void EffectGraph::plan(const Layer& layer, const EffectRegistry& registry, FrameIndex localTime,
                       f32 texelScale, const LayerPlacement& placement,
                       EffectResources* resources, EffectPlan& out) {
    out.clear();
    out.placement = placement;

    // 1. Resolve os valores no instante e tira quem não contribui.
    for (u32 i = 0; i < layer.effects.size(); ++i) {
        const EffectInstance& inst = layer.effects[i];
        if (!inst.enabled) continue;

        const Effect* effect = registry.find(inst.type);
        const ParameterRegistry* params = registry.params(inst.type);
        if (!effect || !params) {
            // Projeto de uma versão com um efeito que esta não tem: fica no
            // projeto, não desenha, e o painel mostra por quê.
            ++out.droppedUnknown;
            out.blockers.emplace_back(i, "efeito desconhecido nesta versao");
            continue;
        }

        const u32 offset = static_cast<u32>(out.values.size());
        for (u32 p = 0; p < params->count(); ++p) {
            out.values.push_back(evaluate_param(layer.tracks, inst, p, params->at(p), localTime));
        }

        EffectEval e;
        e.effect = effect;
        e.instance = &inst;
        e.resources = resources;
        e.values = out.values.data() + offset;
        e.valueOffset = offset;
        e.count = params->count();
        e.effectIndex = i;
        e.localTime = localTime;
        e.texelScale = texelScale;
        e.placement = &out.placement;

        if (effect->is_identity(e)) {
            ++out.droppedIdentity;
            out.values.resize(offset);
            continue;
        }

        ColorOp op;
        if (effect->effect_class() == EffectClass::PerPixel && !effect->color_op(e, op)) {
            // Efeito por pixel que não produziu operação: é identidade na
            // prática (ex.: curvas sem LUT disponível no teste sem GPU).
            ++out.droppedIdentity;
            out.values.resize(offset);
            continue;
        }
        out.evals.push_back(e);
        out.colorOps.push_back(op);
    }

    // Os ponteiros para `values` só ficam estáveis depois que o vetor parou
    // de crescer.
    for (EffectEval& e : out.evals) e.values = out.values.data() + e.valueOffset;

    // 2. Transform no fim da pilha não precisa de passe: entra na matriz da
    //    composição. É a regra "transform não cria textura desnecessária".
    if (!out.evals.empty()) {
        const EffectEval& last = out.evals.back();
        Mat4 m = Mat4::identity();
        f32 opacity = 1.0f;
        if (last.effect->fold_into_composite(last, m, opacity)) {
            out.hasFold = true;
            out.foldMatrix = m;
            out.foldOpacity = opacity;
            out.evals.pop_back();
            out.colorOps.pop_back();
        }
    }

    // 3. Agrupa: por pixel consecutivos viram UM passe (até o limite do
    //    shader, e com no máximo uma curva — o passe tem um slot de LUT).
    usize i = 0;
    while (i < out.evals.size()) {
        const EffectEval& first = out.evals[i];
        if (first.effect->effect_class() == EffectClass::PerPixel) {
            EffectStage st;
            st.kind = EffectStage::Kind::FusedColor;
            st.begin = static_cast<u32>(i);
            bool curveUsed = false;
            while (i < out.evals.size()
                   && out.evals[i].effect->effect_class() == EffectClass::PerPixel) {
                const bool isCurve = out.colorOps[i].code == ColorOpCode::Curves;
                if (st.count == kMaxFusedColorOps) {
                    out.blockers.emplace_back(out.evals[i].effectIndex,
                                              "passe de cor cheio (12 operacoes): novo passe");
                    break;
                }
                if (isCurve && curveUsed) {
                    out.blockers.emplace_back(out.evals[i].effectIndex,
                                              "segunda curva: um LUT por passe, novo passe");
                    break;
                }
                curveUsed = curveUsed || isCurve;
                ++st.count;
                ++i;
            }
            out.fusedEffects += st.count;
            out.stages.push_back(st);
            continue;
        }
        EffectStage st;
        st.kind = EffectStage::Kind::Single;
        st.begin = static_cast<u32>(i);
        st.count = 1;
        out.stages.push_back(st);
        if (first.effect->effect_class() == EffectClass::Domain && i + 1 < out.evals.size()) {
            out.blockers.emplace_back(first.effectIndex,
                                      "efeito de dominio: muda a geometria, quebra a fusao");
        }
        ++i;
    }

    // 4. Margens: quanto de vizinhança as etapas SEGUINTES leem. Uma etapa
    //    que recorta a própria saída ao quadro visível precisa deixar isto.
    f32 margin = 0.0f;
    for (usize s = out.stages.size(); s-- > 0;) {
        EffectStage& st = out.stages[s];
        st.margin = margin;
        for (u32 k = 0; k < st.count; ++k) {
            const EffectEval& e = out.evals[st.begin + k];
            margin += e.effect->input_margin(e);
        }
    }

    // 5. O que só valia sob o lock deixa de ser acessível.
    for (EffectEval& e : out.evals) {
        e.instance = nullptr;
        e.resources = nullptr;
    }
}

// =============================================================================
// Montagem
// =============================================================================
namespace {

struct ColorStackUniforms {
    f32 header[4];
    f32 ops[kMaxFusedColorOps * 16];
};
static_assert(sizeof(ColorStackUniforms) == 16 + kMaxFusedColorOps * 64,
              "layout std140 do color_stack.frag");
static_assert(sizeof(ColorStackUniforms) <= binding::kMaxUniformBytes);

} // namespace

Status EffectGraph::build(const EffectPlan& plan, EffectBuildContext& ctx,
                          const LayerImage& input, LayerImage& out) {
    LayerImage cur = input;

    for (const EffectStage& st : plan.stages) {
        if (st.kind == EffectStage::Kind::FusedColor) {
            ColorStackUniforms u{};
            u.header[0] = static_cast<f32>(st.count);
            TextureHandle lut{};
            for (u32 k = 0; k < st.count; ++k) {
                const ColorOp& op = plan.colorOps[st.begin + k];
                f32* dst = u.ops + k * 16;
                std::memcpy(dst, op.p, sizeof(op.p));
                dst[0] = static_cast<f32>(static_cast<u32>(op.code));
                if (op.code == ColorOpCode::Curves) lut = op.lut;
            }
            LayerImage next = cur;
            next.texture = ctx.texture("cor-fundida", cur.width, cur.height);
            // O bloco vai inteiro: o intervalo amarrado cobre o bloco declarado
            // no shader, e o laço lê só as `count` operações válidas.
            const u32 pass = ctx.fullscreen_pass(
                "cor-fundida", PassStage::Effects, next.texture, ShaderId::effects_color_stack_frag,
                {PassTexture{cur.texture, {}, CommonSampler::NearestClamp},
                 PassTexture{{}, lut, CommonSampler::LinearClamp}},
                &u, sizeof(u));
            if (pass == kInvalidIndex) {
                // Sem pipeline o passe não existe: a layer segue sem os efeitos
                // de cor em vez de desenhar com estado inválido.
                continue;
            }
            cur = next;
            continue;
        }

        EffectEval e = plan.evals[st.begin];
        e.values = plan.values.data() + e.valueOffset;
        e.placement = &plan.placement;
        LayerImage next;
        const Status s = e.effect->build(ctx, e, cur, st.margin, next);
        if (!s.ok() || !next.valid()) {
            AUREA_LOG_WARN("efeito '%s' nao montou os passes: pulado", e.effect->info().name);
            continue;
        }
        cur = next;
    }

    out = cur;
    return OkStatus;
}

// =============================================================================
// Registro
// =============================================================================
Status EffectRegistry::add(std::unique_ptr<Effect> effect) {
    if (!effect) return Errc::InvalidArgument;
    const EffectTypeId id = effect->type_id();
    for (const Entry& e : entries_) {
        if (e.id == id) {
            AUREA_LOG_ERROR("efeito com chave repetida: %s", effect->info().key);
            return Errc::AlreadyExists;
        }
    }
    Entry entry;
    entry.id = id;
    effect->declare_parameters(entry.params);
    entry.effect = std::move(effect);
    entries_.push_back(std::move(entry));
    return OkStatus;
}

const Effect* EffectRegistry::find(EffectTypeId id) const noexcept {
    for (const Entry& e : entries_) if (e.id == id) return e.effect.get();
    return nullptr;
}

const ParameterRegistry* EffectRegistry::params(EffectTypeId id) const noexcept {
    for (const Entry& e : entries_) if (e.id == id) return &e.params;
    return nullptr;
}

EffectTypeId EffectRegistry::find_key(std::string_view key) const noexcept {
    const EffectTypeId id = effect_type_id(key);
    return find(id) ? id : 0;
}

void EffectRegistry::collect_pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const {
    out.push_back(PipelineKey::fullscreen(ShaderId::effects_color_stack_frag, work));
    for (const Entry& e : entries_) e.effect->pipelines(out, work);
}

} // namespace aurea
