// =============================================================================
//  Aurea / effects / builtin / FaceRigEffect.cpp
//
//  Rig 2D de rosto (`aurea.rig.face`, effects/FaceRig.hpp). No planejamento a
//  grade (linhas uniformes + as linhas-chave dos olhos e da boca) é deformada
//  pelo campo analítico na CPU e vai para a GPU como textura de dado: um texel
//  RGBA32F por vértice da grade, (x, y) deformado e (x, y) de repouso em px da
//  camada. O vértice monta os dois triângulos de cada célula pelo
//  gl_VertexIndex; o fragmento amostra a camada no repouso e pinta a pele da
//  pálpebra, a linha dos cílios do olho fechado e o miolo da boca aberta.
// =============================================================================
#include "BuiltinEffects.hpp"
#include "aurea/effects/FaceRig.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace aurea {
namespace builtin {
namespace {

using namespace face_rig;

/// Bloco std140 de face_rig.vert/.frag (o mesmo nos dois estágios).
struct FaceRigUniforms {
    Vec4 region;        ///< saída (px da camada)
    Vec4 inputRegion;   ///< entrada (px da camada)
    Vec4 info;          ///< vértices por linha, largura da textura de dado
    Vec4 eye[2];        ///< centro, meia largura, meia altura (repouso)
    Vec4 eyeK;          ///< fechamento de cada olho (0..1)
    Vec4 skin[2];       ///< dois pontos de pele acima de cada olho (uv da entrada)
    Vec4 lash;          ///< cílios de cima de cada olho (uv: xy, zw)
    Vec4 mouth;         ///< centro, meia largura, meia altura
    Vec4 mouthK;        ///< abertura, topo e base da faixa da linha
    Vec4 mouthUv;       ///< linha da boca (xy) e bochecha (zw), uv da entrada
    Vec4 skinLow[2];    ///< 1.0.8: dois pontos de pele abaixo de cada olho (bochecha)
};

constexpr u32 kGridLines = 56;

Anchors anchors_of(const EffectEval& e) noexcept {
    Anchors a;
    a.face = e.p2(kFaceCenter);
    a.faceR = e.p2(kFaceSize);
    a.eye[0] = e.p2(kEyeLeft);
    a.eyeR[0] = e.p2(kEyeLeftSize);
    a.eye[1] = e.p2(kEyeRight);
    a.eyeR[1] = e.p2(kEyeRightSize);
    a.mouth = e.p2(kMouth);
    a.mouthR = e.p2(kMouthSize);
    a.neck = e.p2(kNeck);
    a.hairTop = e.p2(kHairTop);
    return a;
}

Pose pose_of(const EffectEval& e) noexcept {
    Pose p;
    p.turn = clampf(e.f(kTurn), -60.0f, 60.0f) * kDeg2Rad;
    p.nod = clampf(e.f(kNod), -60.0f, 60.0f) * kDeg2Rad;
    p.tilt = clampf(e.f(kTilt), -60.0f, 60.0f) * kDeg2Rad;
    p.eyeOpen[0] = saturate(e.f(kEyeOpenLeft));
    p.eyeOpen[1] = saturate(e.f(kEyeOpenRight));
    p.mouthOpen = saturate(e.f(kMouthOpen));
    p.smile = clampf(e.f(kSmile), -1.0f, 1.0f);
    p.brow[0] = clampf(e.f(kBrowLeft), -1.0f, 1.0f);
    p.brow[1] = clampf(e.f(kBrowRight), -1.0f, 1.0f);
    p.sway = clampf(e.f(kBodySway), -30.0f, 30.0f) * kDeg2Rad;
    const f64 fps = e.framesPerSecond > 0.0 && std::isfinite(e.framesPerSecond) ? e.framesPerSecond : 30.0;
    p.breath = breath_at(e.f(kBreathing), e.f(kBreathingRate), e.time_frames() / fps);
    return p;
}

bool neutral_params(const EffectEval& e) noexcept {
    if (e.count < kParamCount) return true;
    return e.f(kTurn) == 0.0f && e.f(kNod) == 0.0f && e.f(kTilt) == 0.0f && e.f(kEyeOpenLeft) >= 1.0f
        && e.f(kEyeOpenRight) >= 1.0f && e.f(kMouthOpen) <= 0.0f && e.f(kSmile) == 0.0f && e.f(kBrowLeft) == 0.0f
        && e.f(kBrowRight) == 0.0f && e.f(kBodySway) == 0.0f && !(e.f(kBreathing) > 0.0f);
}

class FaceRigEffect final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kFaceRigKey, "Rig 2D de rosto", "Distort", EffectClass::Domain};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("head_turn_x", "Virar a cabeça", 0.0f, -30.0f, 30.0f, kParamAnimatable, "°");
        p.add_float("nod_y", "Acenar", 0.0f, -30.0f, 30.0f, kParamAnimatable, "°");
        p.add_float("tilt_z", "Inclinar", 0.0f, -30.0f, 30.0f, kParamAnimatable, "°");
        p.add_float("eye_open_left", "Olho esquerdo aberto", 1.0f, 0.0f, 1.0f);
        p.add_float("eye_open_right", "Olho direito aberto", 1.0f, 0.0f, 1.0f);
        p.add_float("mouth_open", "Abrir a boca", 0.0f, 0.0f, 1.0f);
        p.add_float("mouth_smile", "Sorriso", 0.0f, -1.0f, 1.0f);
        p.add_float("brow_left", "Sobrancelha esquerda", 0.0f, -1.0f, 1.0f);
        p.add_float("brow_right", "Sobrancelha direita", 0.0f, -1.0f, 1.0f);
        p.add_float("body_sway", "Corpo", 0.0f, -10.0f, 10.0f, kParamAnimatable, "°");
        p.add_float("breathing", "Respiração", 0.0f, 0.0f, 1.0f);
        p.add_float("breathing_speed", "Velocidade da respiração", 15.0f, 4.0f, 40.0f, kParamAnimatable, "/min");
        // Âncoras (fração da camada): o padrão é o rosto centrado em close.
        static const Anchors d = proportional_anchors(0.2f, 0.12f, 0.8f, 0.78f);
        p.add_point2("face_center", "Centro do rosto", d.face, -1.0f, 2.0f, kParamHidden);
        p.add_point2("face_size", "Tamanho do rosto", d.faceR, 0.0f, 2.0f, kParamHidden);
        p.add_point2("eye_left", "Olho esquerdo", d.eye[0], -1.0f, 2.0f, kParamHidden);
        p.add_point2("eye_left_size", "Tamanho do olho esquerdo", d.eyeR[0], 0.0f, 1.0f, kParamHidden);
        p.add_point2("eye_right", "Olho direito", d.eye[1], -1.0f, 2.0f, kParamHidden);
        p.add_point2("eye_right_size", "Tamanho do olho direito", d.eyeR[1], 0.0f, 1.0f, kParamHidden);
        p.add_point2("mouth", "Boca", d.mouth, -1.0f, 2.0f, kParamHidden);
        p.add_point2("mouth_size", "Tamanho da boca", d.mouthR, 0.0f, 1.0f, kParamHidden);
        p.add_point2("neck", "Queixo", d.neck, -1.0f, 2.0f, kParamHidden);
        p.add_point2("hair_top", "Topo do cabelo", d.hairTop, -1.0f, 2.0f, kParamHidden);
        p.add_float("placed", "Pontos", 0.0f, 0.0f, 3.0f, kParamHidden);
        // v2 (partes separadas): a física do cabelo só aparece no Controle do
        // rig (parts_rig = 1); na imagem de uma folha só ela não teria o que mover.
        ParamSpec hair;
        hair.id = "hair_physics";
        hair.label = "Física do cabelo";
        hair.type = ParamType::Float;
        hair.flags = kParamAnimatable;
        hair.defaultValue = ParamValue::scalar(0.6f);
        hair.minValue = 0.0f;
        hair.maxValue = 1.0f;
        hair.visibleIfParam = kPartsRig;
        hair.visibleIfValue = 1.0f;
        (void)p.add(hair);
        p.add_float("parts_rig", "Partes", 0.0f, 0.0f, 1.0f, kParamHidden);
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat f) const override {
        out.push_back(PipelineKey::graphics(ShaderId::effects_face_rig_vert, ShaderId::effects_face_rig_frag, f));
    }

    bool needs_full_input() const noexcept override { return true; }

    bool demo_values(EffectInstance& instance, std::vector<ParamValue>& values) const noexcept override {
        (void)instance;
        if (values.size() < kParamCount) return false;
        // Prévia do catálogo: cabeça virada, olho piscando e boca aberta.
        values[kTurn].v[0] = 18.0f;
        values[kTilt].v[0] = -6.0f;
        values[kEyeOpenLeft].v[0] = 0.0f;
        values[kMouthOpen].v[0] = 0.7f;
        values[kSmile].v[0] = 0.4f;
        return true;
    }

    bool is_identity(const EffectEval& e) const noexcept override { return neutral_params(e); }

    void resolve_resources(EffectEval& e) const noexcept override {
        e.pathSamples.reset();
        e.aux = TextureHandle{};
        if (!e.placement || !e.resources || e.count < kParamCount || neutral_params(e)) return;
        const f32 w = std::max(1.0f, static_cast<f32>(e.placement->layerWidth));
        const f32 h = std::max(1.0f, static_cast<f32>(e.placement->layerHeight));
        const Rig rig = rig_of(anchors_of(e), w, h);
        const Pose pose = pose_of(e);
        std::vector<f32> xs, ys;
        grid_lines(rig, kGridLines, xs, ys);
        const u32 nx = static_cast<u32>(xs.size()), ny = static_cast<u32>(ys.size());
        if (nx < 2 || ny < 2) return;
        const u32 count = nx * ny;
        const u32 tw = std::min<u32>(1024u, count), th = (count + tw - 1) / tw;
        // [0] = cabeçalho (vértices por linha, largura da textura, nº de vértices do passe), [1] = caixa deformada.
        auto samples = std::make_shared<std::vector<Vec4>>();
        samples->resize(2 + static_cast<usize>(tw) * th, Vec4{0, 0, 0, 0});
        f32 minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        u64 key = 1469598103934665603ull ^ 0x464143455249ull;
        for (u32 j = 0; j < ny; ++j)
            for (u32 i = 0; i < nx; ++i) {
                const Vec2 rest{xs[i], ys[j]};
                const Vec2 d = displace(rig, pose, rest);
                minX = std::min(minX, d.x); maxX = std::max(maxX, d.x);
                minY = std::min(minY, d.y); maxY = std::max(maxY, d.y);
                const Vec4 t{d.x, d.y, rest.x, rest.y};
                (*samples)[2 + static_cast<usize>(j) * nx + i] = t;
                u32 bits[4];
                std::memcpy(bits, &t, sizeof bits);
                for (u32 b : bits) key = (key ^ b) * 1099511628211ull;
            }
        key = (key ^ (static_cast<u64>(nx) << 32 | tw)) * 1099511628211ull;
        (*samples)[0] = Vec4{static_cast<f32>(nx), static_cast<f32>(tw), static_cast<f32>((nx - 1) * (ny - 1) * 6), 0.0f};
        (*samples)[1] = Vec4{minX, minY, maxX - minX, maxY - minY};
        e.aux = e.resources->data_texture(key, samples->data() + 2, tw, th);
        e.pathSamples = std::move(samples);
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin,
                 LayerImage& out) const override {
        if (!e.pathSamples || e.pathSamples->size() < 3 || !e.aux.valid() || !e.placement || e.count < kParamCount) {
            out = in;
            return OkStatus;
        }
        const auto& s = *e.pathSamples;
        const u32 vertices = static_cast<u32>(s[0].z);
        if (!vertices || !(s[1].z > 0.0f) || !(s[1].w > 0.0f) || !(in.region.w > 0.0f) || !(in.region.h > 0.0f)) {
            out = in;
            return OkStatus;
        }
        const Rect region = spread_region({s[1].x, s[1].y, s[1].z, s[1].w}, 0.0f, 0.0f, e.placement, margin);
        if (region.w <= 0.0f || region.h <= 0.0f) {
            out = in;
            return OkStatus;
        }
        const f32 w = std::max(1.0f, static_cast<f32>(e.placement->layerWidth));
        const f32 h = std::max(1.0f, static_cast<f32>(e.placement->layerHeight));
        const Rig rig = rig_of(anchors_of(e), w, h);
        auto uv = [&](f32 x, f32 y) {
            return Vec2{(clampf(x, 0.0f, w) - in.region.x) / in.region.w, (clampf(y, 0.0f, h) - in.region.y) / in.region.h};
        };
        FaceRigUniforms u{};
        u.region = {region.x, region.y, region.w, region.h};
        u.inputRegion = {in.region.x, in.region.y, in.region.w, in.region.h};
        u.info = {s[0].x, s[0].y, 0.0f, 0.0f};
        for (u32 i = 0; i < 2; ++i) {
            const Vec2 c = rig.eye[i], r = rig.eyeR[i];
            u.eye[i] = {c.x, c.y, r.x, r.y};
            // Pele: um pouco acima da faixa da pálpebra, dos dois lados do meio.
            const f32 top = c.y - 1.9f * r.y;
            const Vec2 a = uv(c.x - 0.5f * r.x, top), b = uv(c.x + 0.5f * r.x, top);
            u.skin[i] = {a.x, a.y, b.x, b.y};
            // 1.0.8: no desenho, acima do olho há cílios e franja; a bochecha
            // entra na escolha (o frag usa a amostra mais clara das quatro).
            const f32 low = c.y + 2.3f * r.y;
            const Vec2 la = uv(c.x - 0.4f * r.x, low), lb = uv(c.x + 0.4f * r.x, low);
            u.skinLow[i] = {la.x, la.y, lb.x, lb.y};
        }
        u.eyeK = {1.0f - saturate(e.f(kEyeOpenLeft)), 1.0f - saturate(e.f(kEyeOpenRight)), 0.0f, 0.0f};
        const Vec2 l0 = uv(rig.eye[0].x, rig.eye[0].y - 0.8f * rig.eyeR[0].y);
        const Vec2 l1 = uv(rig.eye[1].x, rig.eye[1].y - 0.8f * rig.eyeR[1].y);
        u.lash = {l0.x, l0.y, l1.x, l1.y};
        u.mouth = {rig.mouth.x, rig.mouth.y, rig.mouthR.x, rig.mouthR.y};
        const f32 half = std::max(0.35f * std::max(rig.mouthR.y, 0.5f), 0.75f);
        u.mouthK = {saturate(e.f(kMouthOpen)), rig.mouth.y - half, rig.mouth.y + half, 0.0f};
        const Vec2 line = uv(rig.mouth.x, rig.mouth.y), cheek = uv(rig.mouth.x + 1.8f * rig.mouthR.x, rig.mouth.y);
        u.mouthUv = {line.x, line.y, cheek.x, cheek.y};
        u32 tw = 0, th = 0;
        ctx.region_size(region, in.texel_scale_x(), tw, th);
        if (!tw || !th) {
            out = in;
            return OkStatus;
        }
        out = {ctx.texture(info().key, tw, th), region, tw, th};
        if (ctx.geometry_pass(info().key, PassStage::Effects, out.texture, ShaderId::effects_face_rig_vert,
                              ShaderId::effects_face_rig_frag,
                              {PassTexture{in.texture, {}, CommonSampler::LinearBorder},
                               PassTexture{{}, e.aux, CommonSampler::NearestClamp}},
                              &u, sizeof(u), vertices, false, false) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

} // namespace

void register_face_rig_effect(EffectRegistry& r) { (void)r.add(std::make_unique<FaceRigEffect>()); }

} // namespace builtin
} // namespace aurea
