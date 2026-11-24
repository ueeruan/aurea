// =============================================================================
//  Aurea / effects / builtin / FaceRigPartEffect.cpp
//
//  Parte de um Rig 2D separado (`aurea.rig.part`, effects/FaceRigParts.hpp).
//  Cada camada de parte (cabelo de trás, rosto, olho, boca, franja, óculos...)
//  lê os parâmetros do "Controle do rig" — o aurea.rig.face do nulo — no
//  instante do quadro (EffectResources::layer_effect_values) e deforma a sua
//  imagem por uma grade como o v1: profundidade própria (paralaxe), inclinar em
//  volta do pescoço, corpo e respiração, a pálpebra que desce sobre o olho
//  (o branco e a íris são CORTADOS por ela no fragmento, não achatados), a boca
//  que abre mostrando o miolo e a física do cabelo (mola avaliada numa janela
//  fixa de tempo: preview = export, sem estado entre quadros).
// =============================================================================
#include "BuiltinEffects.hpp"
#include "aurea/effects/FaceRigParts.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

namespace aurea {
namespace builtin {
namespace {

using namespace face_rig;

/// Bloco std140 de face_rig_part.vert/.frag (o mesmo nos dois estágios).
struct FaceRigPartUniforms {
    Vec4 region;        ///< saída (px da camada)
    Vec4 inputRegion;   ///< entrada (px da camada)
    Vec4 info;          ///< vértices por linha, largura da textura de dado
    Vec4 eye;           ///< olho (px da parte): centro, meia largura, meia altura
    Vec4 lid;           ///< fechamento, topo e linha de baixo (frações do raio y), modo
    Vec4 mouth;         ///< centro, meia largura, meia altura
    Vec4 mouthK;        ///< abertura, topo e base da faixa da linha
    Vec4 mouthUv;       ///< linha da boca (uv da entrada)
};

constexpr u32 kGridLines = 36;

const char* const kRoleLabels[kPartRoleCount] = {
    "Fundo", "Cabelo de trás", "Corpo", "Pescoço", "Rosto", "Orelhas",
    "Branco do olho esquerdo", "Branco do olho direito", "Íris esquerda", "Íris direita",
    "Cílios esquerdos", "Cílios direitos", "Sobrancelha esquerda", "Sobrancelha direita",
    "Nariz", "Boca", "Franja", "Mechas", "Óculos",
};

Anchors anchors_of(const ParamValue* v) noexcept {
    auto p2 = [&](u32 i) { return v[i].as_vec2(); };
    Anchors a;
    a.face = p2(kFaceCenter);
    a.faceR = p2(kFaceSize);
    a.eye[0] = p2(kEyeLeft);
    a.eyeR[0] = p2(kEyeLeftSize);
    a.eye[1] = p2(kEyeRight);
    a.eyeR[1] = p2(kEyeRightSize);
    a.mouth = p2(kMouth);
    a.mouthR = p2(kMouthSize);
    a.neck = p2(kNeck);
    a.hairTop = p2(kHairTop);
    return a;
}

/// Pose dos valores do Controle (mesma regra do aurea.rig.face).
Pose pose_of(const ParamValue* v, f64 seconds) noexcept {
    auto f = [&](u32 i) { return v[i].v[0]; };
    Pose p;
    p.turn = clampf(f(kTurn), -60.0f, 60.0f) * kDeg2Rad;
    p.nod = clampf(f(kNod), -60.0f, 60.0f) * kDeg2Rad;
    p.tilt = clampf(f(kTilt), -60.0f, 60.0f) * kDeg2Rad;
    p.eyeOpen[0] = saturate(f(kEyeOpenLeft));
    p.eyeOpen[1] = saturate(f(kEyeOpenRight));
    p.mouthOpen = saturate(f(kMouthOpen));
    p.smile = clampf(f(kSmile), -1.0f, 1.0f);
    p.brow[0] = clampf(f(kBrowLeft), -1.0f, 1.0f);
    p.brow[1] = clampf(f(kBrowRight), -1.0f, 1.0f);
    p.sway = clampf(f(kBodySway), -30.0f, 30.0f) * kDeg2Rad;
    p.breath = breath_at(f(kBreathing), f(kBreathingRate), seconds);
    return p;
}

u32 role_of(const EffectEval& e) noexcept {
    return std::min<u32>(static_cast<u32>(std::max(0.0f, std::round(e.f(kPartParamRole)))), kPartRoleCount - 1);
}

/// Instante do quadro em quadros da composição.
f64 comp_time(const EffectEval& e) noexcept {
    if (std::isfinite(e.compositionTime)) return e.compositionTime;
    if (!e.layer) return e.time_frames();
    return e.time_frames() + static_cast<f64>(e.layer->start.value) - static_cast<f64>(e.layer->offset.value);
}

class FaceRigPartEffect final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kFaceRigPartKey, "Parte do rig 2D", "Distort", EffectClass::Domain};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        p.add_enum("role", "Parte", kRoleLabels, kPartRoleCount, kPartFace);
        p.add_float("depth", "Profundidade", 0.0f, -1.0f, 1.0f, kParamAnimatable);
        p.add_layer_ref("controller", "Controle do rig", kParamRefNull);
        p.add_point2("origin", "Origem", Vec2{0.0f, 0.0f}, -65536.0f, 65536.0f, kParamHidden);
        p.add_point2("full_size", "Tamanho da imagem", Vec2{1.0f, 1.0f}, 1.0f, 65536.0f, kParamHidden);
        p.add_point2("lid", "Abertura do olho", Vec2{-0.6f, 0.7f}, -2.0f, 2.0f, kParamHidden);
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat f) const override {
        out.push_back(PipelineKey::graphics(ShaderId::effects_face_rig_part_vert, ShaderId::effects_face_rig_part_frag, f));
    }

    bool needs_full_input() const noexcept override { return true; }

    bool is_identity(const EffectEval& e) const noexcept override {
        return e.count < kPartParamCount || e.values[kPartParamController].ref == 0;
    }

    void resolve_resources(EffectEval& e) const noexcept override {
        e.pathSamples.reset();
        e.aux = TextureHandle{};
        e.auxInfo = Vec4{};
        if (!e.placement || !e.resources || !e.layer || e.count < kPartParamCount) return;
        const u64 ref = e.values[kPartParamController].ref;
        if (!ref) return;
        const u32 type = effect_type_id(kFaceRigKey);
        const f64 now = comp_time(e);
        const f64 fps = e.framesPerSecond > 0.0 && std::isfinite(e.framesPerSecond) ? e.framesPerSecond : 30.0;
        std::array<ParamValue, kParamCount> cv{};
        if (e.resources->layer_effect_values(e.layer, ref, type, now, cv.data(), kParamCount) < kParamCount) return;
        const u32 role = role_of(e);
        const f32 depth = e.f(kPartParamDepth);
        const Vec2 origin = e.p2(kPartParamOrigin);
        const Vec2 full = e.p2(kPartParamFullSize);
        const Vec2 lid = e.p2(kPartParamLid);
        const f32 fullW = std::max(1.0f, full.x), fullH = std::max(1.0f, full.y);
        const f64 seconds = e.time_frames() / fps;
        const Rig rig = rig_of(anchors_of(cv.data()), fullW, fullH);
        const Pose pose = pose_of(cv.data(), seconds);
        // Física do cabelo: a mola segue a raiz do cabelo pelos parâmetros do
        // Controle nos instantes anteriores (janela fixa: sem estado).
        Vec2 swing{};
        if (part_is_hair(role)) {
            const f32 amount = saturate(cv[kHairPhysics].v[0]);
            if (amount > 0.0f) {
                std::array<ParamValue, kParamCount> pv{};
                auto drive = [&](f64 t) {
                    const f64 at = now + (t - seconds) * fps;
                    if (e.resources->layer_effect_values(e.layer, ref, type, at, pv.data(), kParamCount) < kParamCount) return Vec2{};
                    return head_drive(rig_of(anchors_of(pv.data()), fullW, fullH), pose_of(pv.data(), t));
                };
                swing = hair_swing(drive, seconds, amount);
            }
        }
        const bool still = is_neutral(pose) && std::fabs(swing.x) < 1e-3f && std::fabs(swing.y) < 1e-3f;
        if (still) return;
        // A grade em px da PARTE (o recorte começa em `origin` da imagem original).
        const f32 w = std::max(1.0f, static_cast<f32>(e.placement->layerWidth));
        const f32 h = std::max(1.0f, static_cast<f32>(e.placement->layerHeight));
        Rig local = rig;
        auto shift = [&](Vec2& v) { v = v - origin; };
        shift(local.face); shift(local.eye[0]); shift(local.eye[1]); shift(local.mouth); shift(local.neck); shift(local.hairTop);
        local.width = w;
        local.height = h;
        std::vector<f32> xs, ys;
        grid_lines(local, kGridLines, xs, ys);
        const u32 nx = static_cast<u32>(xs.size()), ny = static_cast<u32>(ys.size());
        if (nx < 2 || ny < 2) return;
        const u32 count = nx * ny;
        const u32 tw = std::min<u32>(1024u, count), th = (count + tw - 1) / tw;
        auto samples = std::make_shared<std::vector<Vec4>>();
        samples->resize(3 + static_cast<usize>(tw) * th, Vec4{0, 0, 0, 0});
        f32 minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        u64 key = 1469598103934665603ull ^ 0x50415254ull ^ (static_cast<u64>(role) << 40);
        for (u32 j = 0; j < ny; ++j)
            for (u32 i = 0; i < nx; ++i) {
                const Vec2 rest{xs[i], ys[j]};
                const Vec2 d = part_displace(rig, pose, role, depth, swing, lid, rest + origin) - origin;
                minX = std::min(minX, d.x); maxX = std::max(maxX, d.x);
                minY = std::min(minY, d.y); maxY = std::max(maxY, d.y);
                const Vec4 t{d.x, d.y, rest.x, rest.y};
                (*samples)[3 + static_cast<usize>(j) * nx + i] = t;
                u32 bits[4];
                std::memcpy(bits, &t, sizeof bits);
                for (u32 b : bits) key = (key ^ b) * 1099511628211ull;
            }
        key = (key ^ (static_cast<u64>(nx) << 32 | tw)) * 1099511628211ull;
        (*samples)[0] = Vec4{static_cast<f32>(nx), static_cast<f32>(tw), static_cast<f32>((nx - 1) * (ny - 1) * 6), 0.0f};
        (*samples)[1] = Vec4{minX, minY, maxX - minX, maxY - minY};
        // [2] = pose do fragmento: fechamento do olho da parte e abertura da boca.
        f32 close = 0.0f;
        if (role == kPartEyeWhiteL || role == kPartIrisL) close = 1.0f - pose.eyeOpen[0];
        if (role == kPartEyeWhiteR || role == kPartIrisR) close = 1.0f - pose.eyeOpen[1];
        (*samples)[2] = Vec4{close, role == kPartMouth ? pose.mouthOpen : 0.0f, 0.0f, 0.0f};
        // A pose do rig (âncoras do Controle) vai junto para a montagem.
        e.auxInfo = Vec4{rig.eye[0].x, rig.eye[0].y, rig.eye[1].x, rig.eye[1].y};
        auto rigBox = std::make_shared<std::vector<Vec4>>(std::move(*samples));
        rigBox->push_back(Vec4{rig.eyeR[0].x, rig.eyeR[0].y, rig.eyeR[1].x, rig.eyeR[1].y});
        rigBox->push_back(Vec4{rig.mouth.x, rig.mouth.y, rig.mouthR.x, rig.mouthR.y});
        rigBox->push_back(Vec4{rig.neck.x, rig.neck.y, origin.x, origin.y});
        e.aux = e.resources->data_texture(key, rigBox->data() + 3, tw, th);
        e.pathSamples = std::move(rigBox);
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin,
                 LayerImage& out) const override {
        if (!e.pathSamples || e.pathSamples->size() < 6 || !e.aux.valid() || !e.placement || e.count < kPartParamCount) {
            out = in;
            return OkStatus;
        }
        const auto& s = *e.pathSamples;
        const usize tail = s.size() - 3;
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
        const u32 role = role_of(e);
        const Vec2 origin{s[tail + 2].z, s[tail + 2].w};
        FaceRigPartUniforms u{};
        u.region = {region.x, region.y, region.w, region.h};
        u.inputRegion = {in.region.x, in.region.y, in.region.w, in.region.h};
        u.info = {s[0].x, s[0].y, 0.0f, 0.0f};
        const Vec2 lid = e.p2(kPartParamLid);
        const bool eyeRole = role == kPartEyeWhiteL || role == kPartEyeWhiteR || role == kPartIrisL || role == kPartIrisR;
        if (eyeRole) {
            const u32 eye = (role == kPartEyeWhiteR || role == kPartIrisR) ? 1u : 0u;
            const Vec2 c = (eye ? Vec2{e.auxInfo.z, e.auxInfo.w} : Vec2{e.auxInfo.x, e.auxInfo.y}) - origin;
            const Vec2 r = eye ? Vec2{s[tail].z, s[tail].w} : Vec2{s[tail].x, s[tail].y};
            u.eye = {c.x, c.y, std::max(r.x, 0.5f), std::max(r.y, 0.5f)};
            u.lid = {saturate(s[2].x), lid.x, lid.y, s[2].x > 0.0f ? 1.0f : 0.0f};
        }
        if (role == kPartMouth && s[2].y > 0.0f) {
            const Vec2 c = Vec2{s[tail + 1].x, s[tail + 1].y} - origin;
            const Vec2 r{std::max(s[tail + 1].z, 0.5f), std::max(s[tail + 1].w, 0.5f)};
            u.mouth = {c.x, c.y, r.x, r.y};
            const f32 half = std::max(0.35f * r.y, 0.75f);
            u.mouthK = {saturate(s[2].y), c.y - half, c.y + half, 0.0f};
            u.mouthUv = {(c.x - in.region.x) / in.region.w, (c.y - in.region.y) / in.region.h, 0.0f, 0.0f};
            u.lid.w = 2.0f;
        }
        u32 tw = 0, th = 0;
        ctx.region_size(region, in.texel_scale_x(), tw, th);
        if (!tw || !th) {
            out = in;
            return OkStatus;
        }
        out = {ctx.texture(info().key, tw, th), region, tw, th};
        if (ctx.geometry_pass(info().key, PassStage::Effects, out.texture, ShaderId::effects_face_rig_part_vert,
                              ShaderId::effects_face_rig_part_frag,
                              {PassTexture{in.texture, {}, CommonSampler::LinearBorder},
                               PassTexture{{}, e.aux, CommonSampler::NearestClamp}},
                              &u, sizeof(u), vertices, false, false) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

} // namespace

void register_face_rig_part_effect(EffectRegistry& r) { (void)r.add(std::make_unique<FaceRigPartEffect>()); }

} // namespace builtin
} // namespace aurea
