// =============================================================================
//  Aurea / render / ParticleScene.cpp  —  AUREA PARTICULAR no espaço da cena (8.2)
//
//  O lado C++ de render/ParticleScene.hpp: o que o shader analítico precisa
//  para pôr a partícula no lugar certo quando ela sai do "2D de sempre".
//
//  PREPARE (sob o lock): decide os bits (3D, mundo, emissão no nascimento,
//  desfoque), monta o HISTÓRICO da camada e as subamostras do desenho.
//
//  O histórico cobre [t − vida máxima, t] em quadros INTEIROS do tempo local,
//  numa grade alinhada ao passo (quadro f0 múltiplo do passo): a amostra de
//  um quadro f é sempre a MESMA conta sobre a timeline, não importa de qual
//  quadro ela foi lida — prévia = export, seek ida e volta sem diferença. Até
//  128 amostras; janela maior aumenta o passo (2, 3... quadros) e o shader
//  interpola entre elas.
//
//  A taxa animada vira uma AGENDA FIXA na taxa máxima da camada (maior valor
//  dos keyframes) + aceitação por sorteio determinístico: com a agenda presa,
//  mudar a taxa não empurra os nascimentos de quem já nasceu. A taxa máxima é
//  a da camada inteira, e não a da janela: uma taxa máxima que mudasse com t
//  mudaria o período de todos os slots de um quadro para o outro.
//
//  RENDER: sobe o histórico de todas as composições num buffer só (binding
//  16, anel de buffers como glifos/máscaras/vetores), monta o push de cada
//  subamostra e os desenhos das partículas que moram DENTRO da cena 3D.
// =============================================================================
#include "aurea/render/ParticleScene.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/scene3d/SceneRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea {

// Renderer.cpp: as MESMAS contas de mundo e câmera, num instante fracionário
// da timeline.
Mat4 particle_world_3d_at(const Composition& comp, const Layer& l, f64 time) noexcept;
Mat4 particle_world_2d_at(const Composition& comp, const Layer& l, f64 time) noexcept;
scene3d::SceneCamera particle_camera_at(const Composition& comp, f64 time, u32 w, u32 h) noexcept;

namespace {

/// Clip ← px (y para baixo nos dois; o mesmo `clip_from_comp` do Renderer).
Mat4 clip_from_px(f32 w, f32 h) noexcept {
    Mat4 m;
    m.col[0] = Vec4{2.0f / std::max(w, 1.0f), 0, 0, 0};
    m.col[1] = Vec4{0, 2.0f / std::max(h, 1.0f), 0, 0};
    m.col[3] = Vec4{-1.0f, -1.0f, 0, 1};
    return m;
}

/// Clip ← mundo e os eixos da câmera no mundo (billboard): a MESMA projeção
/// que a cena usa (Z reverso, proporção do alvo).
void camera_clip(const scene3d::SceneCamera& cam, u32 w, u32 h, Mat4& clip, Vec4& right, Vec4& down) noexcept {
    clip = scene3d::reverse_z_perspective(cam.fovY, static_cast<f32>(std::max(1u, w)) / static_cast<f32>(std::max(1u, h)), cam.nearZ)
         * cam.view;
    // Linhas 0 e 1 da vista = eixos X e Y (para baixo) da câmera no mundo.
    right = Vec4{cam.view.col[0].x, cam.view.col[1].x, cam.view.col[2].x, 0};
    down = Vec4{cam.view.col[0].y, cam.view.col[1].y, cam.view.col[2].y, 0};
}

/// Um ParticleParam no instante (a mesma leitura de `sampled_particles`).
f32 pparam(const Layer& l, ParticleParam p, FrameIndex t, f32 fallback) noexcept {
    const u32 idx = static_cast<u32>(p);
    if (!l.tracks.find(TrackProperty::ParticleParam, kInvalidIndex, idx)) return fallback;
    return l.tracks.sample_or(TrackProperty::ParticleParam, t, fallback, kInvalidIndex, idx);
}

bool param_animated(const Layer& l, ParticleParam p) noexcept {
    const Track* tr = l.tracks.find(TrackProperty::ParticleParam, kInvalidIndex, static_cast<u32>(p));
    return tr && tr->animated();
}

/// Velocidade da camada (px/s) no quadro local `f` — a mesma diferença de um
/// quadro que o prepare usa para "herdar movimento" (bloco 17).
Vec2 layer_motion(const Layer& l, i64 f, f64 fps) noexcept {
    const FrameIndex a{f}, b{f + 1};
    const f32 x0 = l.tracks.sample_or(TrackProperty::PositionX, a, l.transform.position.x);
    const f32 y0 = l.tracks.sample_or(TrackProperty::PositionY, a, l.transform.position.y);
    const f32 x1 = l.tracks.sample_or(TrackProperty::PositionX, b, l.transform.position.x);
    const f32 y1 = l.tracks.sample_or(TrackProperty::PositionY, b, l.transform.position.y);
    const f32 dt = static_cast<f32>(1.0 / std::max(1.0, fps));
    return Vec2{(x1 - x0) / dt, -(y1 - y0) / dt};
}

} // namespace

// =============================================================================
// Prepare
// =============================================================================
void Renderer::prepare_particle_space(const Composition& comp, const Layer& l, FrameIndex local,
                                      const RenderSettings& settings, bool in3d, RenderLayer& rl,
                                      FrameSnapshot& out) const noexcept {
    namespace ps_ = particle_space;
    ParticleSpace& ps = rl.particle;
    ps = ParticleSpace{};
    const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
    ps.fps = static_cast<f32>(fps);
    const ParticleData pd = sampled_particles(l, local);
    const bool world = pd.emitterSpace == 1;
    const MotionBlurSettings& mb = comp.motion_blur();
    const bool blur = l.motionBlur && mb.enabled && mb.shutterAngle > 0.0f;
    // Emissão animada: taxa, velocidade, direção, espalhamento, offset do
    // emissor — e a posição da camada quando a partícula herda o movimento.
    auto posAnimated = [&](TrackProperty p) {
        const Track* tr = l.tracks.find(p);
        return tr && tr->animated();
    };
    const bool emitAnim = param_animated(l, ParticleParam::Rate) || param_animated(l, ParticleParam::Speed)
                       || param_animated(l, ParticleParam::Direction) || param_animated(l, ParticleParam::Spread)
                       || param_animated(l, ParticleParam::EmitterOffsetX) || param_animated(l, ParticleParam::EmitterOffsetY)
                       || (pd.inheritVelocity != 0.0f && (posAnimated(TrackProperty::PositionX) || posAnimated(TrackProperty::PositionY)));
    u32 flags = 0;
    if (in3d) flags |= ps_::k3D;
    if (world) flags |= ps_::kWorld;
    if (emitAnim) flags |= ps_::kEmit;
    if (blur) flags |= ps_::kMoving;
    if (flags == 0) return;   // o 2D de sempre: nenhum buffer, nenhuma conta nova
    flags |= ps_::kBuffer;
    ps.flags = flags;
    ps.blur = blur;
    // O desfoque é por subamostra de TEMPO no shader: a cópia deslocada da
    // textura (desfoque de transform das camadas) não entra aqui.
    if (blur) rl.blurMatrices.clear();

    // Dentro da cena 3D (profundidade contra modelos e planos) quando nada
    // exige a textura da camada: efeitos, máscaras, matte, modo de mistura,
    // eco. Senão, billboards num alvo do tamanho da composição.
    bool fx = false;
    for (const EffectInstance& e : l.effects) fx |= e.enabled;
    ps.inScene = in3d && !fx && l.masks.empty() && rl.matteMode == MatteMode::None && !rl.matteOnly
              && rl.blend == BlendMode::Normal && rl.temporal.empty();
    ps.compSpace = !ps.inScene && (in3d || world || blur);

    // Saída ← camada num quadro LOCAL (fracionário): mundo no 3D, composição
    // no espaço da composição, a própria camada no resto.
    const f64 toTimeline = static_cast<f64>(l.start.value) - static_cast<f64>(l.offset.value);
    auto outFrom = [&](f64 localFrame) -> Mat4 {
        const f64 T = localFrame + toTimeline;
        if (in3d) return particle_world_3d_at(comp, l, T);
        if (ps.compSpace) return particle_world_2d_at(comp, l, T);
        return Mat4::identity();
    };

    // --- Janela do histórico ---------------------------------------------------
    const f64 tl = static_cast<f64>(local.value);
    const f64 open = blur ? std::clamp(static_cast<f64>(mb.shutterAngle), 0.0, 720.0) / 360.0 : 0.0;   // quadros
    const f64 life = static_cast<f64>(std::clamp(pd.lifetime, 0.05f, 60.0f)) * (1.0 + std::clamp(static_cast<f64>(pd.lifeRandom), 0.0, 1.0));
    const f64 auxLife = pd.auxCount > 0 ? std::max(0.0, static_cast<f64>(pd.auxLife)) : 0.0;
    const f64 span = (life + auxLife) * fps + open * 0.5 + 2.0;
    i64 f0 = std::max<i64>(0, static_cast<i64>(std::floor(tl - span)));
    const i64 f1 = std::max<i64>(f0, static_cast<i64>(std::ceil(tl + open * 0.5)) + 1);
    i64 step = std::max<i64>(1, (f1 - f0 + static_cast<i64>(ps_::kMaxSamples) - 3) / static_cast<i64>(ps_::kMaxSamples - 2));
    u32 count = 0;
    for (;;) {
        const i64 a = (f0 / step) * step;   // grade alinhada: a amostra do quadro f é a mesma em todo quadro
        const i64 c = (f1 - a + step - 1) / step + 1;
        if (c <= static_cast<i64>(ps_::kMaxSamples)) {
            f0 = a;
            count = static_cast<u32>(c);
            break;
        }
        ++step;
    }

    // --- Taxa máxima (a agenda) ------------------------------------------------
    f32 rateMax = std::max(pd.rate, 0.0f);
    const Track* rateTr = l.tracks.find(TrackProperty::ParticleParam, kInvalidIndex, static_cast<u32>(ParticleParam::Rate));
    const bool rateExpr = rateTr && rateTr->has_expression();
    if (rateTr && !rateTr->keys.empty()) {
        rateMax = 0.0f;
        for (const Keyframe& k : rateTr->keys) rateMax = std::max(rateMax, k.value);
    }

    // --- Bloco: cabeçalho + amostras -------------------------------------------
    ps.dataFirst = static_cast<u32>(out.particleData.size());
    std::vector<Vec4>& D = out.particleData;
    const Mat4 now = outFrom(tl);
    for (int c = 0; c < 4; ++c) D.push_back(now.col[c]);
    const usize infoAt = D.size();
    D.push_back(Vec4{static_cast<f32>(static_cast<f64>(f0) / fps), static_cast<f32>(static_cast<f64>(step) / fps),
                     static_cast<f32>(count), 0.0f});
    // Gravidade e vento em Z (px/s², + = para dentro, o mesmo sentido do Z
    // das camadas: para longe da câmera padrão).
    D.push_back(Vec4{pd.gravity.z, pd.wind.z, 0.0f, 0.0f});
    const bool needRows = world || blur;
    const f32 lw = static_cast<f32>(comp.width()), lh = static_cast<f32>(comp.height());
    const ParticleData& base = l.particles;
    for (u32 i = 0; i < count; ++i) {
        const i64 f = f0 + static_cast<i64>(i) * step;
        const Mat4 M = needRows ? outFrom(static_cast<f64>(f)) : now;
        D.push_back(Vec4{M.col[0].x, M.col[1].x, M.col[2].x, M.col[3].x});
        D.push_back(Vec4{M.col[0].y, M.col[1].y, M.col[2].y, M.col[3].y});
        D.push_back(Vec4{M.col[0].z, M.col[1].z, M.col[2].z, M.col[3].z});
        if (emitAnim) {
            const FrameIndex t{f};
            const f32 rate = std::max(0.0f, pparam(l, ParticleParam::Rate, t, base.rate));
            if (rateExpr) rateMax = std::max(rateMax, rate);
            D.push_back(Vec4{rate, pparam(l, ParticleParam::Speed, t, base.speed),
                             pparam(l, ParticleParam::Direction, t, base.direction) * kDeg2Rad,
                             pparam(l, ParticleParam::Spread, t, base.spread) * kDeg2Rad});
            const Vec2 mv = layer_motion(l, f, fps);
            D.push_back(Vec4{lw * 0.5f + pparam(l, ParticleParam::EmitterOffsetX, t, base.emitterOffset.x),
                             lh * 0.5f + pparam(l, ParticleParam::EmitterOffsetY, t, base.emitterOffset.y), mv.x, mv.y});
        } else {
            D.push_back(Vec4{});
            D.push_back(Vec4{});
        }
    }
    rateMax = std::clamp(rateMax, 0.1f, 1000000.0f);
    D[infoAt].w = rateMax;

    // Emissão animada: a agenda roda na taxa MÁXIMA (o shader aceita cada
    // nascimento com taxa(nascimento)/máxima). Slots refeitos com ela — a
    // mesma conta do prepare.
    if (emitAnim) {
        const f32 lifeNow = rl.source.particleBlock[0].y;
        const u32 cap = std::max<u32>(1u, static_cast<u32>(static_cast<f64>(pd.maxParticles)
                                                            * std::clamp(resolve_heavy(settings).particles, 0.05f, 1.0f)));
        const u32 auxMul = 1u + std::min<u32>(pd.auxCount, 16u);
        const u32 flowSlots = std::min<u32>(cap / auxMul, static_cast<u32>(std::ceil(rateMax * lifeNow * 1.25f)) + 1u);
        const u32 slots = std::min<u32>(cap / auxMul, flowSlots + pd.burst);
        rl.source.particleBlock[0].x = rateMax;
        rl.source.particleBlock[3].w = static_cast<f32>(slots);
        rl.source.particleBlock[18].y = static_cast<f32>(flowSlots);
        rl.source.particleBlock[19].x = static_cast<f32>(flowSlots);
        rl.source.particleSlots = std::min<u32>(cap, slots * auxMul);
    }

    // --- Subamostras do desenho ------------------------------------------------
    // Na cena, a câmera e o deslocamento vêm do (sub)quadro da cena. Fora
    // dela: K instantes do obturador (o mesmo nº de amostras das camadas;
    // prévia reduzida pela qualidade, export completo), média aditiva.
    const u32 k = (blur && !ps.inScene)
        ? std::clamp<u32>(settings.finalQuality ? mb.samples
                                                : static_cast<u32>(static_cast<f32>(mb.previewSamples) * std::clamp(settings.heavyScale, 0.1f, 1.0f)),
                          2u, 64u)
        : 1u;
    ps.subs.resize(k);
    for (u32 s = 0; s < k; ++s) {
        const f64 shiftFrames = k > 1 ? ((static_cast<f64>(s) + 0.5) / static_cast<f64>(k) - 0.5) * open : 0.0;
        ParticleSub& sub = ps.subs[s];
        sub.shift = static_cast<f32>(shiftFrames / fps);
        if (in3d) {
            const scene3d::SceneCamera cam = particle_camera_at(comp, tl + shiftFrames + toTimeline, out.compWidth, out.compHeight);
            camera_clip(cam, out.compWidth, out.compHeight, sub.clip, sub.right, sub.down);
        } else {
            sub.clip = clip_from_px(static_cast<f32>(rl.source.width), static_cast<f32>(rl.source.height));
        }
    }
}

// =============================================================================
// Render
// =============================================================================
void Renderer::upload_particle_history(FrameSnapshot& snap) noexcept {
    // Todos os históricos do quadro (pré-composições incluídas) num buffer só.
    usize total = 0;
    std::vector<FrameSnapshot*>& all = snapAll_;
    std::vector<FrameSnapshot*>& stack = snapStack_;
    all.clear();
    stack.assign(1, &snap);
    while (!stack.empty()) {
        FrameSnapshot* s = stack.back();
        stack.pop_back();
        s->particleBase = static_cast<u32>(total);
        total += s->particleData.size();
        all.push_back(s);
        for (auto& c : s->nested) if (c) stack.push_back(c.get());
    }
    particleFrameBuf_ = BufferHandle{};
    if (total == 0) return;
    const u32 slot = particleSlot_++ % kGlyphRing;
    const usize bytes = total * sizeof(Vec4);
    if (particleCap_[slot] < bytes) {
        if (particleBuf_[slot].valid()) backend_->destroy_buffer(particleBuf_[slot]);
        BufferDesc bd;
        bd.bytes = std::max<usize>(bytes * 2, 1024 * sizeof(Vec4));
        bd.usage = BufferUsage::Storage;
        bd.access = MemoryAccess::Upload;
        bd.debugName = "particulas-historico";
        auto b = backend_->create_buffer(bd);
        particleBuf_[slot] = b.ok() ? *b : BufferHandle{};
        particleCap_[slot] = b.ok() ? bd.bytes : 0;
    }
    void* ptr = nullptr;
    if (!particleBuf_[slot].valid() || !backend_->map_buffer(particleBuf_[slot], ptr).ok() || !ptr) return;
    auto* dst = static_cast<Vec4*>(ptr);
    for (FrameSnapshot* s : all) std::copy(s->particleData.begin(), s->particleData.end(), dst + s->particleBase);
    backend_->unmap_buffer(particleBuf_[slot]);
    particleFrameBuf_ = particleBuf_[slot];
}

ParticlePush Renderer::particle_push(const RenderLayer& layer, u32 sub, f32 weight) const noexcept {
    ParticlePush p;
    const ParticleSpace& ps = layer.particle;
    if (ps.flags == 0 || ps.subs.empty() || !currentSnap_) {
        p.clip = clip_from_px(static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height));
        p.mode.w = weight;
        return p;
    }
    const ParticleSub& s = ps.subs[std::min<usize>(sub, ps.subs.size() - 1)];
    p.clip = s.clip;
    p.mode = Vec4{static_cast<f32>(ps.flags), s.shift, static_cast<f32>(currentSnap_->particleBase + ps.dataFirst), weight};
    p.camRight = s.right;
    p.camDown = s.down;
    return p;
}

bool Renderer::particle_quad_ready() noexcept {
    if (particleQuad_.valid()) return true;
    BufferDesc bd;
    bd.bytes = 6 * sizeof(u16);
    bd.usage = BufferUsage::Index | BufferUsage::TransferDst;
    bd.access = MemoryAccess::GpuOnly;
    bd.debugName = "particulas-quad";
    auto b = backend_->create_buffer(bd);
    if (!b.ok()) return false;
    const u16 idx[6] = {0, 1, 2, 1, 3, 2};
    if (!backend_->write_buffer(*b, 0, idx, sizeof(idx)).ok()) {
        backend_->destroy_buffer(*b);
        return false;
    }
    particleQuad_ = *b;
    return true;
}

u32 Renderer::scene_particle_draws(const scene3d::SceneFrame& group, const scene3d::SceneFrame& frame,
                                   scene3d::SceneParticleDraw*& out) noexcept {
    out = nullptr;
    if (!currentSnap_ || group.particleLayers.empty() || !particleFrameBuf_.valid() || !particle_quad_ready()) return 0;
    const u32 n = static_cast<u32>(group.particleLayers.size());
    out = arena_.alloc_array<scene3d::SceneParticleDraw>(n);
    if (!out) return 0;
    // A MESMA viewProj dos modelos e planos: câmera do (sub)quadro, proporção do alvo.
    Mat4 clip;
    Vec4 right, down;
    camera_clip(frame.camera, compTargetW_, compTargetH_, clip, right, down);
    u32 used = 0;
    for (u32 idx : group.particleLayers) {
        if (idx >= currentSnap_->layers.size()) continue;
        const RenderLayer& layer = currentSnap_->layers[idx];
        const ParticleSpace& ps = layer.particle;
        if (!ps.inScene || layer.source.kind != LayerSource::Kind::Particles || layer.source.particleSlots == 0) continue;
        // 8.2: dados extras (fonte, textura, malha, curvas) também na cena.
        const ParticleExtrasBind px = particle_extras_bind(layer.source, renderFrameNumber_);
        PipelineKey key = PipelineKey::graphics(ShaderId::particles_particles_vert, ShaderId::particles_particles_frag,
                                                SurfaceFormat::RGBA16F, true,
                                                layer.source.particleAdditive ? BlendMode::Add : BlendMode::Normal);
        // Testa o depth de modelos e planos, não escreve nele: partícula
        // translúcida/aditiva não tapa o que vem atrás dela.
        key.hasDepth = true;
        key.depthTest = true;
        key.depthWrite = px.mesh;   // a partícula de MALHA é sólida: escreve (as malhas se ocluem)
        key.depthCompare = CompareOp::GreaterOrEqual;
        key.depthFormat = SurfaceFormat::Depth32F;
        key.cull = CullMode::None;
        auto pipe = shaders_.pipeline(key);
        if (!pipe.ok()) {
            incomplete_ = true;
            continue;
        }
        auto* uni = arena_.alloc_array<Vec4>(LayerSource::kParticleBlocks);
        if (!uni) continue;
        std::copy(layer.source.particleBlock, layer.source.particleBlock + LayerSource::kParticleBlocks, uni);
        particle_extras_params(px, uni);
        ParticlePush push;
        push.clip = clip;
        // Subquadro do desfoque da cena: a partícula anda no tempo só com o
        // desfoque DELA ligado (a câmera do subquadro vale para todos).
        const f64 shift = ps.blur ? frame.subFrame / std::max(1.0, static_cast<f64>(ps.fps)) : 0.0;
        push.mode = Vec4{static_cast<f32>(ps.flags), static_cast<f32>(shift),
                         static_cast<f32>(currentSnap_->particleBase + ps.dataFirst), 1.0f};
        push.camRight = right;
        push.camDown = down;
        scene3d::SceneParticleDraw& d = out[used++];
        d = scene3d::SceneParticleDraw{};
        d.pipeline = *pipe;
        d.uniforms = uni;
        d.uniformBytes = static_cast<u32>(sizeof(Vec4) * LayerSource::kParticleBlocks);
        static_assert(sizeof(ParticlePush) <= sizeof(d.push), "push das partículas");
        std::memcpy(d.push, &push, sizeof(push));
        d.pushBytes = sizeof(push);
        d.history = particleFrameBuf_;
        d.quad = particleQuad_;
        d.instances = layer.source.particleSlots;
        d.extras = px.buffer;
        d.texture = px.texture;
        d.sampler = shaders_.sampler(CommonSampler::LinearClamp).id;
        d.meshVertices = px.mesh ? px.meshVertices : 0u;
        // Uma vez por quadro (não por subquadro do desfoque) nos contadores 8E.
        if (&frame == &group || (!group.blurFrames.empty() && &frame == &group.blurFrames.front())) {
            heavyStats_.lastParticleSlots += layer.source.particleSlots;
            ++heavyStats_.lastParticleLayers;
        }
    }
    return used;
}

} // namespace aurea
