#include "aurea/effects/EffectGraph.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/timeline/Composition.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace aurea {

// =============================================================================
// Utilidades de Effect.hpp
// =============================================================================
Rect visible_layer_rect(const LayerPlacement& pl) noexcept {
    // Camada na CENA 3D: a matriz 2D da composição não diz onde ela aparece (o
    // plano dela vive no mundo, sob a câmera), então não há retângulo visível a
    // calcular. Vazio = "não corte por visibilidade" — quem chama já trata
    // assim, e era o corte que encolhia a região para 1x1 e fazia a camada
    // desaparecer atrás do efeito (Motion Tile, Meio-tom, brilhos...).
    if (pl.inScene3d || pl.preserveFullExtent) return Rect{0, 0, 0, 0};
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

const LayerImage* EffectBuildContext::layer_input(u64 layer) const noexcept {
    if (!layer) return nullptr;
    const LayerId want = LayerId::unpack(layer);
    for (const LayerInput& in : layerInputs_) {
        if (!in.image.valid()) continue;
        if (in.layer == layer) return &in.image;
        // Pré-composição: o renderer troca a parte alta do índice pelo sal
        // (0x40000000 | sal << 16 | índice & 0xFFFF); a geração fica.
        const LayerId got = LayerId::unpack(in.layer);
        if ((got.index & 0x40000000u) && got.generation == want.generation
            && (got.index & 0xFFFFu) == (want.index & 0xFFFFu)) return &in.image;
    }
    return nullptr;
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

    // Passes de efeito leem no máximo 4 texturas; a captura fica pequena o
    // bastante para o InplaceFunction (sem alocação por passe).
    constexpr u32 kEffectTextures = 4;
    struct Bind { FGTexture graph; u64 raw; u64 sampler; };
    struct Capture {
        PipelineHandle pipeline;
        Bind binds[kEffectTextures];
        u32 count;
        const void* uniforms;
        u32 uniformBytes;
    } cap{};
    cap.pipeline = *pipe;
    cap.uniforms = u;
    cap.uniformBytes = uniformBytes;
    for (const PassTexture& t : textures) {
        if (cap.count >= kEffectTextures) break;
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

u32 EffectBuildContext::geometry_pass(const char* name, PassStage stage, FGTexture target, ShaderId vertex,
                                        ShaderId fragment, std::initializer_list<PassTexture> textures,
                                        const void* uniforms, u32 uniformBytes, u32 vertexCount, bool depth,
                                        bool blend, BlendMode blendMode) noexcept {
    const TextureDesc& td = graph_.desc(target);
    PipelineKey key = PipelineKey::graphics(vertex, fragment, td.format, blend, blendMode);
    if (depth) {
        key.hasDepth = true;
        key.depthTest = true;
        key.depthWrite = true;
        key.depthCompare = CompareOp::GreaterOrEqual;   // Z reverso: limpa em 0
        key.depthFormat = SurfaceFormat::Depth32F;
    }
    auto pipe = shaders_.pipeline(key);
    if (!pipe.ok()) return kInvalidIndex;
    void* u = nullptr;
    if (uniforms && uniformBytes) {
        u = arena_.alloc(uniformBytes, 16);
        if (!u) return kInvalidIndex;
        std::memcpy(u, uniforms, uniformBytes);
    }
    constexpr u32 kGeometryTextures = 4;
    struct Bind { FGTexture graph; u64 raw; u64 sampler; };
    struct Capture {
        PipelineHandle pipeline;
        Bind binds[kGeometryTextures];
        u32 count;
        const void* uniforms;
        u32 uniformBytes;
        u32 vertices;
    } cap{};
    cap.pipeline = *pipe;
    cap.uniforms = u;
    cap.uniformBytes = uniformBytes;
    cap.vertices = vertexCount;
    for (const PassTexture& t : textures) {
        if (cap.count >= kGeometryTextures) break;
        cap.binds[cap.count++] = Bind{t.graph, t.raw.id, shaders_.sampler(t.sampler).id};
    }
    auto record = [cap](PassContext& pc) {
        pc.cmds.bind_pipeline(cap.pipeline);
        for (u32 i = 0; i < cap.count; ++i) {
            const Bind& b = cap.binds[i];
            const TextureHandle tex = b.graph.valid() ? pc.texture(b.graph) : TextureHandle{b.raw};
            if (tex.valid()) pc.cmds.bind_texture(i, tex, SamplerHandle{b.sampler});
        }
        if (cap.uniforms) pc.cmds.set_uniforms(cap.uniforms, cap.uniformBytes);
        if (cap.vertices) pc.cmds.draw(cap.vertices);
    };
    u32 pass = kInvalidIndex;
    if (depth) {
        TextureDesc dd;
        dd.width = td.width;
        dd.height = td.height;
        dd.format = SurfaceFormat::Depth32F;
        dd.sampled = false;
        dd.renderTarget = true;
        dd.transient = true;
        const FGTexture z = graph_.create_texture("geometria-prof", dd);
        pass = graph_.add_raster_pass_depth(name, stage, target, LoadOp::Clear, Vec4{0, 0, 0, 0}, z, LoadOp::Clear,
                                            false, 0.0f, record);
    } else {
        pass = graph_.add_raster_pass(name, stage, target, LoadOp::Clear, Vec4{0, 0, 0, 0}, record);
    }
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
    foldEffectIndex = kInvalidIndex;
    droppedIdentity = 0;
    droppedUnknown = 0;
    fusedEffects = 0;
}

// =============================================================================
// Planejamento
// =============================================================================
void EffectGraph::plan(const Layer& layer, const EffectRegistry& registry, FrameIndex localTime,
                       f32 texelScale, const LayerPlacement& placement,
                       EffectResources* resources, EffectPlan& out, f64 framesPerSecond, const Composition* composition) {
    plan_f(layer, registry, static_cast<f64>(localTime.value), texelScale, placement, resources, out, framesPerSecond, composition);
}

void EffectGraph::plan_f(const Layer& layer, const EffectRegistry& registry, f64 localTime,
                         f32 texelScale, const LayerPlacement& placement,
                         EffectResources* resources, EffectPlan& out, f64 framesPerSecond, const Composition* composition) {
    out.clear();
    out.placement = placement;

    struct Source { const Layer* owner; const EffectInstance* effect; f64 time; };
    // Memória de trabalho do planejamento, reaproveitada entre quadros (o
    // caminho quente não aloca: Perf8C.SteadyPlaybackOf50Layers...).
    static thread_local std::vector<Source> sources;
    static thread_local std::vector<Mat4> downstream;
    static thread_local std::vector<u8> hasDownstream;
    static thread_local std::vector<ParamValue> scratch;
    sources.clear();
    for (const auto& effect : layer.effects) sources.push_back({&layer, &effect, localTime});
    const f64 globalTime = localTime + layer.start.value - layer.offset.value;
    const FrameIndex global{static_cast<i64>(std::floor(globalTime))};
    // A null has no pixels. Its Motion Tile controls the raster of each child;
    // sample the null's animation clock, not the child's trimmed/remapped clock.
    const Layer* parent = composition ? composition->layer(layer.parent) : nullptr;
    for (u32 depth = 0; parent && parent != &layer && depth < 16; ++depth) {
        if (parent->kind == LayerKind::Null && parent->contains_time(global))
            for (const auto& effect : parent->effects)
                if (effect.type == effect_type_id(effect_keys::kMotionTile))
                    sources.push_back({parent, &effect, globalTime - parent->start.value + parent->offset.value});
        parent = composition->layer(parent->parent);
    }
    // 0. A geometria afim DEPOIS de cada efeito (de trás para a frente): o
    //    produto dos Transform/Oscilar/Agitar seguintes, dobráveis ou não. Um
    //    efeito que mede o quadro visível (Motion Tile, os recortes pela área
    //    visível) precisa da matriz que leva a saída DELE ao quadro:
    //    camada · geometria seguinte. Só avalia os efeitos que dobram.
    downstream.assign(sources.size(), Mat4::identity());
    hasDownstream.assign(sources.size(), 0);
    {
        Mat4 acc = Mat4::identity();
        bool any = false;
        for (usize i = sources.size(); i-- > 0;) {
            downstream[i] = acc;
            hasDownstream[i] = any ? 1 : 0;
            const auto& source = sources[i];
            const EffectInstance& inst = *source.effect;
            if (!inst.enabled) continue;
            const Effect* effect = registry.find(inst.type);
            const ParameterRegistry* params = registry.params(inst.type);
            if (!effect || !params || effect->effect_class() != EffectClass::Domain) continue;
            scratch.clear();
            for (u32 p = 0; p < params->count(); ++p)
                scratch.push_back(evaluate_param_f(source.owner->tracks, inst, p, params->at(p), source.time));
            EffectEval e;
            e.effect = effect;
            e.instance = &inst;
            e.values = scratch.data();
            e.count = params->count();
            e.effectIndex = static_cast<u32>(i);
            e.localTime = FrameIndex{static_cast<i64>(std::floor(source.time))};
            e.fractionalTime = source.time;
            e.framesPerSecond = std::isfinite(framesPerSecond) && framesPerSecond > 0 ? framesPerSecond : 30.0;
            e.texelScale = texelScale;
            e.placement = &out.placement;
            e.layer = &layer;
            Mat4 m = Mat4::identity();
            f32 opacity = 1.0f;
            if (effect->fold_into_composite(e, m, opacity)) {
                acc = acc * m;   // T_n · … · T_i: o mais tardio fica à esquerda
                any = true;
            }
        }
    }
    LayerPlacement stepPlacement = placement;
    // 1. Resolve os valores no instante e tira quem não contribui.
    for (u32 i = 0; i < sources.size(); ++i) {
        const auto& source = sources[i];
        const EffectInstance& inst = *source.effect;
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
            out.values.push_back(evaluate_param_f(source.owner->tracks, inst, p, params->at(p), source.time));
        }

        EffectEval e;
        e.effect = effect;
        e.instance = &inst;
        e.resources = resources;
        e.values = out.values.data() + offset;
        e.valueOffset = offset;
        e.count = params->count();
        e.effectIndex = i;
        e.localTime = FrameIndex{static_cast<i64>(std::floor(source.time))};
        e.fractionalTime = source.time;
        e.framesPerSecond = std::isfinite(framesPerSecond) && framesPerSecond > 0 ? framesPerSecond : 30.0;
        e.texelScale = texelScale;
        e.layer = &layer;
        // Identidade e recursos medem o quadro com a geometria SEGUINTE: um
        // Motion Tile a 100% numa camada que cobre o quadro deixa de ser
        // neutro quando um Transform depois dele a encolhe.
        stepPlacement.compFromLayer = hasDownstream[i] ? placement.compFromLayer * downstream[i] : placement.compFromLayer;
        e.placement = &stepPlacement;

        if (effect->is_identity(e)) {
            Mat4 identityFold = Mat4::identity();
            f32 identityOpacity = 1.f;
            if (effect->fold_into_composite(e, identityFold, identityOpacity)) out.foldEffectIndex = i;
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
        // O que só existe sob o lock (espectro do som) entra no eval agora.
        effect->resolve_resources(e);
        out.foldEffectIndex = kInvalidIndex;
        e.placement = &out.placement;
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
            out.foldEffectIndex = last.effectIndex;
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

    // 3b. A geometria seguinte de cada etapa (ver EffectStage::after). Por
    //     pixel não mede o quadro: só as etapas de um efeito levam a matriz.
    for (EffectStage& st : out.stages) {
        if (st.kind != EffectStage::Kind::Single) continue;
        const u32 src = out.evals[st.begin].effectIndex;
        if (src < downstream.size() && hasDownstream[src]) {
            st.after = downstream[src];
            st.hasAfter = true;
        }
    }

    // 4. Margens: quanto de vizinhança as etapas SEGUINTES leem. Uma etapa
    //    que recorta a própria saída ao quadro visível precisa deixar isto.
    f32 margin = 0.0f;
    bool fullExtent = false;
    for (usize s = out.stages.size(); s-- > 0;) {
        EffectStage& st = out.stages[s];
        st.margin = margin;
        st.preserveFullExtent = fullExtent;
        for (u32 k = 0; k < st.count; ++k) {
            const EffectEval& e = out.evals[st.begin + k];
            // Tiling and surface deformation can bring offscreen pixels back
            // into view; clipping a previous stage would discard their input.
            if (e.effect->needs_full_input()) fullExtent = true;
            // A reader's radius is in this stage's OUTPUT coordinates.
            // Propagate it through the inverse affine before asking earlier
            // stages for pixels. Tile -> scale 10% -> blur 24px needs 240px
            // of tile padding, not 24px. Absolute inverse row sums bound
            // both axes, including rotation, reflection and uneven scale.
            if (margin > 0.0f && e.effect->effect_class() == EffectClass::Domain) {
                Mat4 m = Mat4::identity();
                f32 opacity = 1.0f;
                if (e.effect->fold_into_composite(e, m, opacity)) {
                    const f32 a = m.col[0].x, b = m.col[0].y;
                    const f32 c = m.col[1].x, d = m.col[1].y;
                    const f32 det = a * d - b * c;
                    if (std::isfinite(det) && std::fabs(det) >= 1e-9f) {
                        const f32 reach = std::max(std::fabs(d) + std::fabs(c),
                                                   std::fabs(b) + std::fabs(a)) / std::fabs(det);
                        const f32 expanded = margin * reach;
                        if (std::isfinite(expanded)) margin = expanded;
                    }
                }
            }
            margin += e.effect->input_margin(e);
        }
    }

    // 5. O que só valia sob o lock deixa de ser acessível.
    for (EffectEval& e : out.evals) {
        e.instance = nullptr;
        e.resources = nullptr;
        e.layer = nullptr;
    }
}

// =============================================================================
// Montagem
// =============================================================================
namespace {

std::atomic<u64> g_bypassed{0};
/// Tipos que já logaram a falha (log uma vez por tipo; o contador segue).
std::atomic<u32> g_loggedTypes[32]{};

void note_bypass(EffectTypeId type, const char* name) noexcept {
    g_bypassed.fetch_add(1, std::memory_order_relaxed);
    for (auto& slot : g_loggedTypes) {
        u32 cur = slot.load(std::memory_order_relaxed);
        if (cur == type) return;
        if (cur == 0 && slot.compare_exchange_strong(cur, type, std::memory_order_relaxed)) {
            AUREA_LOG_WARN("efeito '%s' falhou ao montar: bypass (a camada segue sem ele)", name ? name : "?");
            return;
        }
        if (cur == type) return;
    }
}

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
    cur.history = FGTexture{};

    // DETECTAR MOVIMENTO: a fonte num instante anterior (quando o renderer a
    // trouxe) passa pelas MESMAS etapas que vêm antes do efeito que a pede —
    // senão uma cor ou um desfoque antes dele virariam "movimento" em todo
    // pixel. Depois da última etapa que pede, a imagem do passado sai de cena.
    usize lastHistory = plan.stages.size();
    for (usize i = 0; i < plan.stages.size(); ++i) {
        const EffectStage& st = plan.stages[i];
        if (st.kind == EffectStage::Kind::Single && plan.evals[st.begin].effect->wants_history()) lastHistory = i;
    }
    LayerImage hist;
    const bool carryHistory = input.history.valid() && lastHistory < plan.stages.size();
    if (carryHistory) hist = LayerImage{input.history, input.region, input.width, input.height};

    // Uma etapa sobre `img`. false = bypass (a imagem segue a mesma).
    auto run = [&](const EffectStage& st, LayerImage& img, bool past) -> bool {
        if (st.kind == EffectStage::Kind::FusedColor) {
            ColorStackUniforms u{};
            u.header[0] = static_cast<f32>(st.count);
            // Tamanho do texel e densidade: a chave de croma com pré-desfoque
            // lê um anel de vizinhos medido em px da camada.
            u.header[1] = img.width > 0 ? 1.0f / static_cast<f32>(img.width) : 0.0f;
            u.header[2] = img.height > 0 ? 1.0f / static_cast<f32>(img.height) : 0.0f;
            u.header[3] = img.texel_scale_x();
            TextureHandle lut{};
            for (u32 k = 0; k < st.count; ++k) {
                const ColorOp& op = plan.colorOps[st.begin + k];
                f32* dst = u.ops + k * 16;
                std::memcpy(dst, op.p, sizeof(op.p));
                dst[0] = static_cast<f32>(static_cast<u32>(op.code));
                if (op.code == ColorOpCode::Curves) lut = op.lut;
            }
            LayerImage next = img;
            next.texture = ctx.texture("cor-fundida", img.width, img.height);
            // O bloco vai inteiro: o intervalo amarrado cobre o bloco declarado
            // no shader, e o laço lê só as `count` operações válidas.
            const u32 pass = ctx.fullscreen_pass(
                "cor-fundida", PassStage::Effects, next.texture, ShaderId::effects_color_stack_frag,
                {PassTexture{img.texture, {}, CommonSampler::NearestClamp},
                 PassTexture{{}, lut, CommonSampler::LinearClamp}},
                &u, sizeof(u));
            if (pass == kInvalidIndex) {
                // Sem pipeline o passe não existe: a layer segue sem os efeitos
                // de cor em vez de desenhar com estado inválido.
                note_bypass(1u, "cor fundida");
                return false;
            }
            img = next;
            return true;
        }

        EffectEval e = plan.evals[st.begin];
        // O passado não se compara com o passado dele: o efeito que pede a
        // história só age na imagem do instante.
        if (past && e.effect->wants_history()) return false;
        e.values = plan.values.data() + e.valueOffset;
        e.placement = &plan.placement;
        LayerPlacement seen;
        if (st.hasAfter || st.preserveFullExtent) {
            // A imagem desta etapa chega ao quadro pela camada E pela
            // geometria dos efeitos seguintes (dobrados ou não).
            seen = plan.placement;
            if (st.hasAfter) seen.compFromLayer = plan.placement.compFromLayer * st.after;
            seen.preserveFullExtent = st.preserveFullExtent;
            e.placement = &seen;
        }
        const bool wants = !past && e.effect->wants_history();
        if (wants) ctx.set_history(carryHistory ? hist : LayerImage{});
        LayerImage next;
        const Status s = e.effect->build(ctx, e, img, st.margin, next);
        if (wants) ctx.set_history(LayerImage{});
        if (!s.ok() || !next.valid()) {
            // Bypass: `img` segue sendo a entrada; o quadro não cai por um efeito.
            note_bypass(e.effect->type_id(), e.effect->info().name);
            return false;
        }
        next.history = FGTexture{};
        img = next;
        return true;
    };

    for (usize i = 0; i < plan.stages.size(); ++i) {
        const EffectStage& st = plan.stages[i];
        (void)run(st, cur, false);
        // A imagem do passado anda junto até a última etapa que a lê.
        if (carryHistory && i < lastHistory) (void)run(st, hist, true);
    }

    out = cur;
    return OkStatus;
}

u64 EffectGraph::bypassed_total() noexcept { return g_bypassed.load(std::memory_order_relaxed); }

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
