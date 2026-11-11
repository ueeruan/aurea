// =============================================================================
//  Aurea / effects / builtin / HeadbopEffect.cpp
//
//  Headbop (`aurea.distort.headbop`, 0.5.2): a pessoa da foto ou do vídeo
//  balança a cabeça e o corpo na batida.
//
//  Planejamento (lock do modelo): a pose de cada pessoa no instante (motor de
//  movimento, HeadbopMotion.hpp, ou os canais "convertidos em keyframes"), o
//  rig (parado nos parâmetros ocultos; por quadro-fonte no RigStore para
//  vídeo), a máscara da pessoa (o mesmo ai::DepthMapService do Rotobrush, em
//  cache por quadro; a exportação espera por ela) e o bloco de uniforms.
//
//  Montagem: com a máscara, o fundo é preenchido em pirâmide (push-pull,
//  ~160 px no lado maior, 2×8 passes pequenos) e UM passe de geometria
//  desenha a pessoa deformada por cima. Sem pessoa: o quadro inteiro balança
//  (solavanco + balanço) com a borda presa.
//
//  Índices gravados nos projetos: nunca reordenar; novos só no fim.
// =============================================================================
#include "BuiltinEffects.hpp"

#include "aurea/effects/Headbop.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace aurea {
namespace builtin {
namespace {

using namespace headbop;

constexpr u32 kUniformVec4 = sizeof(Uniforms) / sizeof(Vec4);
constexpr u32 kPlateLongSide = 160;
constexpr u32 kPlateLevels = 9;

/// Bloco do push-pull (o layout de EffectUniforms).
struct FillUniforms {
    Vec4 uvMap{};
    Vec4 texel{};
    Vec4 p0{};
    Vec4 p1{};
    Vec4 p2{};
    Vec4 p3{};
    Vec4 color{};
};

RigFrame static_rig(const EffectEval& e) noexcept {
    RigFrame r;
    const i32 state = static_cast<i32>(std::lround(e.f(kRigState)));
    r.count = state >= 2 ? static_cast<u32>(std::min(state - 1, static_cast<i32>(kMaxFaces))) : 0u;
    for (u32 i = 0; i < r.count; ++i) {
        const u32 b = kRigFirst + i * kFaceParams;
        RigFace& f = r.faces[i];
        f.center = e.p2(b + 0);
        f.axes = e.p2(b + 1);
        const Vec2 ang = e.p2(b + 2), pd = e.p2(b + 3);
        f.roll = ang.x; f.yaw = ang.y; f.pitch = pd.x; f.depth = pd.y;
        f.neck = e.p2(b + 4);
        f.shoulderA = e.p2(b + 5);
        f.shoulderB = e.p2(b + 6);
        f.confidence = 1.0f;
    }
    return r;
}

class HeadbopEffect final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kHeadbopKey, "Headbop", "Distort", EffectClass::Domain};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kPresets[] = {"Bop", "Aceno", "Balanço", "Quique", "Groove"};
        p.add_enum("preset", "Estilo", kPresets, 5, 0);                                                         // 0
        p.add_float("intensity", "Intensidade", 100, 0, 200, kParamAnimatable | kParamPercent, "%");            // 1
        p.add_float("head", "Cabeça", 100, 0, 200, kParamAnimatable | kParamPercent, "%");                      // 2
        p.add_float("body", "Corpo", 100, 0, 200, kParamAnimatable | kParamPercent, "%");                       // 3
        p.add_float("jolt", "Tranco", 100, 0, 200, kParamAnimatable | kParamPercent, "%");                      // 4
        p.add_float("sway", "Balanço lateral", 100, 0, 200, kParamAnimatable | kParamPercent, "%");             // 5
        p.add_float("turn3d", "Giro 3D", 0, 0, 100, kParamAnimatable | kParamPercent, "%");                     // 6
        p.add_float("delay", "Atraso", 100, 0, 200, kParamAnimatable | kParamPercent, "%");                     // 7
        p.add_float("smoothness", "Suavidade", 100, 25, 300, kParamAnimatable | kParamPercent, "%");            // 8
        p.add_float("variation", "Variação", 100, 0, 200, kParamAnimatable | kParamPercent, "%");               // 9
        p.add_float("phase", "Fase", 0, -4, 4, kParamAnimatable);                                               // 10
        static const char* const kSub[] = {"1/2", "1", "2"};
        p.add_enum("subdivision", "Subdivisão", kSub, 3, 1);                                                    // 11
        p.add_float("beat_zoom", "Zoom na batida", 0, 0, 20, kParamAnimatable | kParamPercent, "%");            // 12
        static const char* const kSources[] = {"Automática no áudio", "BPM manual", "Tap"};
        p.add_enum("beat_source", "Fonte da batida", kSources, 3, 0);                                           // 13
        p.add_float("bpm", "BPM", 120, 40, 240, kParamAnimatable);                                              // 14
        p.typed_range(20, 400);
        p.add_layer_ref("audio_layer", "Camada de áudio");                                                      // 15
        static const char* const kPose[] = {"Batida", "Keyframes"};
        p.add_enum("pose_source", "Fonte da pose", kPose, 2, 0);                                                // 16
        static const char* const kOut[] = {"Normal", "Só a pessoa (transparente)", "Só a pessoa (cor sólida)"};
        p.add_enum("output", "Saída", kOut, 3, 0);                                                              // 17
        p.add_color("solid_color", "Cor", Vec4{1, 1, 1, 1});                                                    // 18
        p.add_bool("motion_blur", "Desfoque de movimento", false);                                              // 19
        p.add_bool("pivot_manual", "Pivô manual", false);                                                       // 20
        p.add_point2("pivot", "Pivô do pescoço", Vec2{0.5f, 0.45f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative); // 21
        p.set_control(kPivot, ParamControl::Point);
        p.add_bool("cutout", "Recortar pessoa", true, kParamNone);                                              // 22
        p.add_int("seed", "Semente", 1, 0, 9999, kParamHidden);                                                 // 23
        static const char* const kChannels[kPoseChannels] = {
            "pose_frame_dy", "pose_frame_zoom", "pose_torso_dx", "pose_torso_dy", "pose_torso_roll", "pose_head_dx",
            "pose_head_dy", "pose_head_pitch", "pose_head_roll", "pose_head_yaw", "pose_extremity", "pose_frame_dx",
            "pose_hair_dx", "pose_hair_dy", "pose_hair_roll", "pose_hair_pitch", "pose_cloth_dx", "pose_cloth_dy",
            "pose_cloth_roll", "pose_flutter_x", "pose_flutter_y", "pose_wave_phase", "pose_wave_amp"};
        for (u32 c = 0; c < 11; ++c)
            p.add_float(kChannels[c], "Pose", 0, -100, 100, kParamAnimatable | kParamHidden);                  // 24..34
        p.add_float("auto_bpm", "BPM (análise)", 0, 0, 400, kParamHidden);                                      // 35
        p.add_float("auto_phase", "Fase (análise)", 0, -100000, 100000, kParamHidden);                          // 36
        p.add_float("tap_bpm", "BPM (tap)", 0, 0, 400, kParamHidden);                                           // 37
        p.add_float("tap_phase", "Fase (tap)", 0, -100000, 100000, kParamHidden);                               // 38
        p.add_float("rig_token", "Rig", 0, 0, 16777215, kParamHidden);                                          // 39
        p.add_float("rig_state", "Rig (estado)", 0, 0, 5, kParamHidden);                                        // 40
        static char ids[kMaxFaces][kFaceParams][24];
        static const char* const kField[kFaceParams] = {"center", "axes", "angles", "pitch", "neck", "shoulder_a", "shoulder_b"};
        for (u32 i = 0; i < kMaxFaces; ++i) {
            for (u32 k = 0; k < kFaceParams; ++k) {
                std::snprintf(ids[i][k], sizeof ids[i][k], "rig%u_%s", i, kField[k]);
                p.add_point2(ids[i][k], "Rig", Vec2{0.5f, 0.5f}, -1000.0f, 1000.0f, kParamHidden);            // 41..68
            }
        }
        // 12º canal da pose (frameDx, 0.5.2): no fim, depois do rig.
        p.add_float(kChannels[11], "Pose", 0, -100, 100, kParamAnimatable | kParamHidden);       // 69
        // Movimento secundário (0.5.2, no fim): o cabelo e a roupa seguem com atraso,
        // passam do ponto e assentam. 0 = só cabeça/tronco rígidos (o desenho de antes).
        p.add_float("hair", "Cabelo", 60, 0, 200, kParamAnimatable | kParamPercent, "%");                     // 70
        p.add_float("cloth", "Roupa", 45, 0, 200, kParamAnimatable | kParamPercent, "%");                     // 71
        p.add_float("physics", "Física", 100, 0, 200, kParamAnimatable | kParamPercent, "%");                 // 72
        for (u32 c = 12; c < kPoseChannels; ++c)
            p.add_float(kChannels[c], "Pose", 0, -100, 100, kParamAnimatable | kParamHidden);                  // 73..83
        // Habilitação cruzada (o motor só lê o que vale na fonte escolhida).
        p.set_enabled_by(kBpm, kBeatSource, 1u << static_cast<u32>(BeatSource::Manual));
        p.set_enabled_by(kAudioLayer, kBeatSource, 1u << static_cast<u32>(BeatSource::Automatic));
        p.set_enabled_by(kSolidColor, kOutput, 1u << static_cast<u32>(Output::PersonSolid));
        p.set_enabled_by(kPivot, kPivotManual, 1u);
    }

    std::span<const EffectPreset> presets() const noexcept override {
        // Cabelo/roupa por estilo: Bop/Groove moderados; Aceno só o cabelo.
        static constexpr EffectPresetValue kBop[] = {{kPreset, 0}, {kHair, 60}, {kCloth, 45}};
        static constexpr EffectPresetValue kNod[] = {{kPreset, 1}, {kHair, 60}, {kCloth, 0}};
        static constexpr EffectPresetValue kSwayP[] = {{kPreset, 2}, {kHair, 50}, {kCloth, 50}};
        static constexpr EffectPresetValue kBounce[] = {{kPreset, 3}, {kHair, 70}, {kCloth, 50}};
        static constexpr EffectPresetValue kGroove[] = {{kPreset, 4}, {kHair, 60}, {kCloth, 45}};
        static const EffectPreset kAll[] = {
            {"bop", "Bop", kBop}, {"nod", "Aceno", kNod}, {"sway", "Balanço", kSwayP},
            {"bounce", "Quique", kBounce}, {"groove", "Groove", kGroove}};
        return kAll;
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat f) const override {
        out.push_back(PipelineKey::graphics(ShaderId::effects_headbop_vert, ShaderId::effects_headbop_frag, f));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_headbop_fill_down_frag, f));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_headbop_fill_up_frag, f));
    }

    bool needs_full_input() const noexcept override { return true; }

    bool demo_values(EffectInstance& instance, std::vector<ParamValue>& values) const noexcept override {
        (void)instance;
        if (values.size() < kParamCount) return false;
        // Prévia do catálogo (foto sem pessoa → quadro inteiro): forte e fora do
        // repouso no instante 0.
        values[kIntensity].v[0] = 200.0f;
        values[kPhase].v[0] = -0.3f;
        values[kBeatZoom].v[0] = 8.0f;
        values[kBeatSource].v[0] = static_cast<f32>(BeatSource::Manual);
        values[kBpm].v[0] = 120.0f;
        return true;
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        if (e.count < kParamCount) return true;
        if (e.e(kOutput) != static_cast<u32>(Output::Normal)) return false;
        if (e.e(kPoseSource) == static_cast<u32>(PoseSource::Keyframes)) return false;
        return !(e.f(kIntensity) > 0.0f) && !(e.f(kBeatZoom) > 0.0f);
    }

    void resolve_resources(EffectEval& e) const noexcept override {
        e.pathSamples.reset();
        e.aux = TextureHandle{};
        e.auxInfo = Vec4{};
        if (e.count < kParamCount) return;
        {
            const f32 W = e.placement && e.placement->layerWidth ? static_cast<f32>(e.placement->layerWidth) : 1.0f;
            const f32 H = e.placement && e.placement->layerHeight ? static_cast<f32>(e.placement->layerHeight) : 1.0f;
            const f64 fps = e.framesPerSecond > 0.0 && std::isfinite(e.framesPerSecond) ? e.framesPerSecond : 30.0;
            const Layer* layer = e.layer;
            const f64 local = e.time_frames();
            const f64 compFrame = layer ? local + static_cast<f64>(layer->start.value - layer->offset.value) : local;
            const f64 t = compFrame / fps;
            const f64 dt = 0.5 / fps;

            // Rig: o parado dos parâmetros; vídeo com trilha → o do quadro-fonte.
            RigFrame rig = static_rig(e);
            std::shared_ptr<const StoreEntry> entry;
            const f32 tokenF = e.f(kRigToken);
            const u32 token = std::isfinite(tokenF) && tokenF >= 1.0f ? static_cast<u32>(std::min(tokenF, 16777215.0f)) : 0u;
            if (token) entry = RigStore::shared().get(token);
            if (entry && !entry->track.frames.empty() && layer && layer->kind == LayerKind::Video) {
                const f64 src = layer->source_frame_f(compFrame) / fps;
                if (std::isfinite(src)) rig = sample_track(entry->track, src);
            }

            // Poses (uma por pessoa; quadro inteiro usa a [0]).
            std::array<Pose, kMaxFaces> pose{}, prev{};
            const u32 people = std::max(1u, rig.count);
            const u32 seed = static_cast<u32>(std::max(0, e.value(kSeed).as_int()));
            if (e.e(kPoseSource) == static_cast<u32>(PoseSource::Keyframes)) {
                f32 ch[kPoseChannels];
                for (u32 c = 0; c < kPoseChannels; ++c) ch[c] = e.f(bake_param(c));
                const Pose base = pose_from_channels(ch);
                // Os canais são do instante; o meio obturador antes repete (sem desfoque).
                for (u32 i = 0; i < people; ++i) {
                    f64 ph = 0.0;
                    f32 gain = 1.0f;
                    person_offset(seed, i, ph, gain);
                    std::array<f32, kPoseChannels> c = pose_channels(base);
                    for (f32& v : c) v *= gain;
                    pose[i] = prev[i] = pose_from_channels(c.data());
                }
            } else {
                f32 vals[kSeed + 1];
                for (u32 k = 0; k <= kSeed; ++k) vals[k] = e.f(k);
                vals[kSeed] = static_cast<f32>(seed);
                Params base = motion_params(vals, kSeed + 1);
                apply_secondary(base, e.f(kHair), e.f(kCloth), e.f(kPhysics));
                const BeatGrid grid = beat_grid(e.e(kBeatSource), e.f(kAutoBpm), e.f(kAutoPhase), e.f(kTapBpm), e.f(kTapPhase), entry.get());
                for (u32 i = 0; i < people; ++i) {
                    Params mp = base;
                    f64 ph = 0.0;
                    f32 gain = 1.0f;
                    person_offset(seed, i, ph, gain);
                    mp.phaseBeats += ph;
                    mp.intensity *= gain;
                    mp.seed = base.seed + i * 7919u;
                    pose[i] = evaluate(mp, grid, t);
                    prev[i] = evaluate(mp, grid, t - dt);
                }
            }

            // Máscara da pessoa (o recorte do Rotobrush, em cache por quadro).
            BuildInput in;
            in.width = W;
            in.height = H;
            in.rig = rig;
            in.pose = pose;
            in.prevPose = prev;
            in.pivotManual = e.b(kPivotManual);
            in.pivot = e.p2(kPivot);
            in.output = static_cast<Output>(std::min<u32>(e.e(kOutput), 2u));
            in.solid = e.color(kSolidColor);
            in.blurSamples = e.b(kMotionBlur) && !low_end_device() ? 5u : 0u;
            in.hair = e.f(kHair) > 0.0f;
            in.cloth = e.f(kCloth) > 0.0f;
            const bool media = layer && (layer->kind == LayerKind::Image || layer->kind == LayerKind::Video);
            if (e.b(kCutout) && rig.count > 0 && media && e.resources) {
                DepthMapRequest req;
                req.host = layer;
                req.instance = e.instance;
                req.localTime = e.localTime;
                req.foreground = true;
                req.smoothing = 0.0f;
                req.stateSalt = 0x4842u;
                const DepthMapResult r = e.resources->depth_map(req);
                e.aux = r.texture;
                e.foregroundSourceTimeUs = r.sourceTimeUs;
                // Sem máscara (modelo indisponível): a deformação contínua, sem recorte.
                in.mask = in.plate = r.texture.valid();
            }
            Uniforms u;
            fill_uniforms(in, u);
            auto samples = std::make_shared<std::vector<Vec4>>(kUniformVec4);
            std::memcpy(samples->data(), &u, sizeof u);
            e.pathSamples = std::move(samples);
        }
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin,
                 LayerImage& out) const override {
        (void)margin;
        if (!e.pathSamples || e.pathSamples->size() < kUniformVec4 || !in.valid()) {
            out = in;
            return OkStatus;
        }
        Uniforms u;
        std::memcpy(&u, e.pathSamples->data(), sizeof u);
        const bool mask = e.aux.valid() && (static_cast<u32>(u.mode.w + 0.5f) & 1u);
        if (!mask) u.mode.w = 0.0f;
        u.region = {in.region.x, in.region.y, in.region.w, in.region.h};
        u.inputRegion = u.region;
        const u32 gx = static_cast<u32>(u.layer.z + 0.5f), gy = static_cast<u32>(u.layer.w + 0.5f);
        const u32 vertices = vertex_count(gx, gy);
        // Entrada maior que a caixa da camada (Motion Tile antes, a parede): as
        // linhas de fora da grade vão até a borda da entrada. Fora da caixa o
        // peso é 0 (a janela da borda prende), então lá a imagem só segue o
        // zoom/solavanco globais — a parede continua inteira.
        if (vertices && gx <= kGridMax && gy <= kGridMax) {
            f32* lx = reinterpret_cast<f32*>(u.gridX);
            f32* ly = reinterpret_cast<f32*>(u.gridY);
            lx[0] = std::min(lx[0], in.region.x);
            lx[gx - 1] = std::max(lx[gx - 1], in.region.x + in.region.w);
            ly[0] = std::min(ly[0], in.region.y);
            ly[gy - 1] = std::max(ly[gy - 1], in.region.y + in.region.h);
        }
        if (!vertices) {
            out = in;
            return OkStatus;
        }

        // Fundo preenchido (push-pull) só com a máscara.
        FGTexture plate{}, plateAlpha{};
        if (mask && (static_cast<u32>(u.mode.w + 0.5f) & 2u)) {
            const f32 W = std::max(1.0f, u.layer.x), H = std::max(1.0f, u.layer.y);
            // Push-pull: alphaPlate = false preenche a COR (só vizinhos opacos);
            // true preenche o ALFA (todo vizinho fora da pessoa), para o buraco
            // de um recorte sobre fundo transparente ficar transparente.
            auto pyramid = [&](bool alphaPlate, FGTexture& result) -> bool {
                u32 w = W >= H ? kPlateLongSide : std::max(2u, static_cast<u32>(std::lround(kPlateLongSide * W / H)));
                u32 h = W >= H ? std::max(2u, static_cast<u32>(std::lround(kPlateLongSide * H / W))) : kPlateLongSide;
                FGTexture down[kPlateLevels]{};
                u32 dw[kPlateLevels]{}, dh[kPlateLevels]{};
                u32 levels = 0;
                FillUniforms f{};
                // fração da camada → uv da entrada
                f.uvMap = {W / std::max(1.0f, in.region.w), H / std::max(1.0f, in.region.h),
                           -in.region.x / std::max(1.0f, in.region.w), -in.region.y / std::max(1.0f, in.region.h)};
                for (; levels < kPlateLevels; ++levels) {
                    dw[levels] = w;
                    dh[levels] = h;
                    down[levels] = ctx.texture("HeadbopFundo", w, h);
                    f.texel = {1.0f / w, 1.0f / h, 0, 0};
                    f.p0 = {levels == 0 ? 1.0f : 0.0f, alphaPlate ? 1.0f : 0.0f, 0, 0};
                    const PassTexture src = levels == 0 ? PassTexture{in.texture, {}, CommonSampler::LinearClamp}
                                                        : PassTexture{down[levels - 1], {}, CommonSampler::LinearClamp};
                    if (ctx.fullscreen_pass("HeadbopFundo", PassStage::Effects, down[levels], ShaderId::effects_headbop_fill_down_frag,
                                            {src, PassTexture{{}, e.aux, CommonSampler::LinearClamp}}, &f, sizeof f) == kInvalidIndex)
                        return false;
                    if (w == 1 && h == 1) { ++levels; break; }
                    w = std::max(1u, (w + 1) / 2);
                    h = std::max(1u, (h + 1) / 2);
                }
                FGTexture coarse = down[levels - 1];
                for (i32 k = static_cast<i32>(levels) - 2; k >= 0; --k) {
                    const FGTexture up = ctx.texture("HeadbopFundoUp", dw[k], dh[k]);
                    if (ctx.fullscreen_pass("HeadbopFundoUp", PassStage::Effects, up, ShaderId::effects_headbop_fill_up_frag,
                                            {PassTexture{down[k], {}, CommonSampler::LinearClamp},
                                             PassTexture{coarse, {}, CommonSampler::LinearClamp}}, nullptr, 0) == kInvalidIndex)
                        return false;
                    coarse = up;
                }
                result = coarse;
                return true;
            };
            if (!pyramid(false, plate) || !pyramid(true, plateAlpha)) return Errc::PipelineCompileFailed;
        } else {
            u.mode.w = mask ? 1.0f : 0.0f;
        }

        out = {ctx.texture(info().key, in.width, in.height), in.region, in.width, in.height};
        const PassTexture maskTex = mask ? PassTexture{{}, e.aux, CommonSampler::LinearClamp}
                                         : PassTexture{in.texture, {}, CommonSampler::LinearClamp};
        const PassTexture plateTex = plate.valid() ? PassTexture{plate, {}, CommonSampler::LinearClamp}
                                                   : PassTexture{in.texture, {}, CommonSampler::LinearClamp};
        if (ctx.geometry_pass(info().key, PassStage::Effects, out.texture, ShaderId::effects_headbop_vert,
                              ShaderId::effects_headbop_frag,
                              {PassTexture{in.texture, {}, CommonSampler::LinearClamp}, maskTex, plateTex,
                               plateAlpha.valid() ? PassTexture{plateAlpha, {}, CommonSampler::LinearClamp} : plateTex},
                              &u, sizeof u, vertices, false, false) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

} // namespace

void register_headbop_effect(EffectRegistry& r) { (void)r.add(std::make_unique<HeadbopEffect>()); }

} // namespace builtin
} // namespace aurea
