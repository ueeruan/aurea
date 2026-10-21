#include "aurea/render/Renderer.hpp"
#include "aurea/effects/RandomEdits.hpp"
#include "aurea/render/PreviewCachePolicy.hpp"
#include "aurea/render/ShutterPlan.hpp"
#include "aurea/render/MotionBlurBounds.hpp"
#include "aurea/media/P010Upload.hpp"
#include "aurea/ai/RotoService.hpp"
#include <bit>
#include "aurea/timeline/GridLayout.hpp"
#include "aurea/render/MaskRaster.hpp"
#include "aurea/audio/Audio.hpp"
#include "aurea/audio/Spectrum.hpp"
#include "aurea/expr/Expression.hpp"

#include "aurea/scene3d/Animation.hpp"
#include "aurea/scene3d/Text3D.hpp"
#include "aurea/text/TextTransform.hpp"
#include "aurea/scene3d/Shape3D.hpp"
#include "aurea/scene3d/PartEffects.hpp"
#include "aurea/scene3d/RigPose.hpp"
#include "aurea/scene3d/StudioEnvironment.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/text/FontManager.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/timeline/LayerAnimator.hpp"
#include "aurea/timeline/Rig.hpp"
#include "aurea/timeline/ShapeGeometry.hpp"
#include "aurea/tracking/MotionGeometry.hpp"
#include "aurea/vector/Vector.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/project/Project.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace aurea {
namespace {

constexpr SurfaceFormat kWorkFormat = SurfaceFormat::RGBA16F;
// Prévia em memória (SDR): 4 B/px. Ver preview_cache_capacity.
constexpr SurfaceFormat kPreviewCacheFormat = SurfaceFormat::RGBA8_sRGB;
constexpr u32 kPreviewCacheBytesPerPixel = 4;

struct CaptureRenderPhases {
    bool enabled;
    u64 started, previous;
    explicit CaptureRenderPhases(bool trace) noexcept
        : enabled(trace), started(trace ? monotonic_ns() : 0), previous(started) {}
    void mark(const char* phase) noexcept {
        if (!enabled) return;
        const u64 now = monotonic_ns();
        AUREA_LOG_INFO("capture-render phase=%s elapsed_ms=%.3f phase_ms=%.3f", phase,
            static_cast<double>(now - started) * 1e-6, static_cast<double>(now - previous) * 1e-6);
        previous = now;
    }
    ~CaptureRenderPhases() { mark("returned"); }
};

// A CFR boundary must land on the last frame's actual timestamp. Using half
// a frame before EOF can be one microsecond outside the decoder's tolerance
// (30 fps: 9,983,334 vs 9,966,667), making an export wait until its timeout.
i64 last_source_timestamp(const VideoStreamInfo& info, i64 frameUs) noexcept {
    if(info.durationUs<=0)return 0;
    if(info.preciseFrameTiming)return info.durationUs-1;
    if(std::isfinite(info.fps)&&info.fps>0){
        const f64 index=std::max(0.0,std::ceil(info.durationUs*info.fps/1e6-1e-6)-1.0);
        return std::clamp<i64>(static_cast<i64>(std::llround(index*1e6/info.fps)),0,info.durationUs-1);
    }
    return std::max<i64>(0,info.durationUs-std::max<i64>(1,frameUs));
}

// Instante da fonte (us) que uma camada de vídeo pede no quadro de fonte
// `sourceFrame` (em quadros da composição): a MESMA conta do prepare — grade
// da fonte quando o arquivo não traz intervalos, preso ao último quadro.
i64 video_media_us(f64 sourceFrame, f64 compFps, const VideoStreamInfo& info, i64 frameUs) noexcept {
    i64 us = static_cast<i64>(std::llround(sourceFrame * 1e6 / compFps));
    if (info.fps > 0.0 && !info.preciseFrameTiming) {
        const f64 idx = std::floor(static_cast<f64>(us) * info.fps / 1e6 + 1e-3);
        us = static_cast<i64>(std::llround(idx * 1e6 / info.fps));
    }
    if (info.durationUs > 0) us = std::clamp<i64>(us, 0, last_source_timestamp(info, frameUs));
    return us;
}

// O quadro APROXIMADO `f` (o cache não tem o de `targetUs`) pode representar
// a camada agora? Só se é um quadro que ela mostra em algum instante: nada de
// antes do ponto de entrada nem de depois do de saída — no corte de um split
// esse quadro é a imagem da outra metade; num seek, o keyframe de fora do
// trecho. `direction` ≠ 0 (camada acabando de entrar, tocando ou arrastando):
// também nada adiante do alvo no sentido em que a fonte anda — um quadro de
// outro ponto deste clipe que sobrou no cache seria o "futuro" piscando antes
// do começo. Exceção: o começo do arquivo (alguns têm o 1º pts > 0, e antes
// dele não existe quadro nenhum — o 1º É a imagem do início).
bool video_fallback_allowed(const Layer& l, const DecodedFrame& f, i64 targetUs, f64 compFps,
                            const VideoStreamInfo& info, i64 frameUs, i32 direction) noexcept {
    frameUs = std::max<i64>(1, frameUs);
    const i64 half = frameUs / 2;
    // Velocidade fixa ou animada (nunca negativa): a fonte é monotônica no
    // trecho, as pontas bastam. A curva de tempo vai e volta: os valores dos
    // keyframes entram também (o trecho fica, no máximo, mais largo).
    f64 lo = l.source_frame(l.start);
    f64 hi = l.source_frame(FrameIndex{std::max(l.start.value, l.end.value - 1)});
    if (lo > hi) std::swap(lo, hi);
    if (l.timeRemapEnabled) {
        for (const Keyframe& k : l.timeRemap.keys) {
            lo = std::min(lo, static_cast<f64>(k.value));
            hi = std::max(hi, static_cast<f64>(k.value));
        }
    }
    if (!std::isfinite(lo) || !std::isfinite(hi)) return true;
    const i64 loUs = video_media_us(lo, compFps, info, frameUs);
    const i64 hiUs = video_media_us(hi, compFps, info, frameUs);
    const i64 begin = f.durationUs > 0 ? f.ptsUs : f.ptsUs - half;
    const i64 end = f.durationUs > 0 ? f.ptsUs + f.durationUs : f.ptsUs + half + 1;
    if (end <= loUs || begin > hiUs) return false;
    if (direction > 0 && f.ptsUs > targetUs && f.ptsUs > 2 * frameUs) return false;
    if (direction < 0 && end <= targetUs) return false;
    return true;
}

/// As máscaras de canal do RGB no tempo: uma amostra contribui exatamente um
/// canal, e as três somadas refazem a cor inteira (o alfa entra dividido por
/// três no shader de composição).
constexpr Vec3 kChannelMasks[3] = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};

f32 srgb_to_linear(f32 c) noexcept {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

/// float → half (IEEE 754 binary16), arredondando para o mais próximo.
u16 to_half(f32 f) noexcept {
    u32 x;
    std::memcpy(&x, &f, 4);
    const u32 sign = (x >> 16) & 0x8000u;
    i32 exp = static_cast<i32>((x >> 23) & 0xFF) - 127 + 15;
    u32 mant = x & 0x7FFFFFu;
    if (exp <= 0) {
        if (exp < -10) return static_cast<u16>(sign);
        mant = (mant | 0x800000u) >> (1 - exp);
        return static_cast<u16>(sign | ((mant + 0x1000u) >> 13));
    }
    if (exp >= 31) return static_cast<u16>(sign | 0x7C00u);
    const u32 h = sign | (static_cast<u32>(exp) << 10) | (mant >> 13);
    return static_cast<u16>(h + ((mant >> 12) & 1u));
}

/// Maior fator de escala de uma matriz 2D (comprimento da maior coluna).
f32 max_scale(const Mat4& m) noexcept {
    const f32 sx = std::sqrt(m.col[0].x * m.col[0].x + m.col[0].y * m.col[0].y);
    const f32 sy = std::sqrt(m.col[1].x * m.col[1].x + m.col[1].y * m.col[1].y);
    return std::max(sx, sy);
}

/// Densidade de trabalho da layer: texels por pixel da layer, arredondada
/// PARA CIMA à potência de 2 (1, 1/2, 1/4...). Duas razões para o degrau:
/// qualidade nunca abaixo do que a tela mostra, e poucas resoluções distintas
/// — uma layer com zoom animado não cria textura nova a cada frame.
f32 texel_scale_for(f32 onScreenScale) noexcept {
    if (!(onScreenScale > 0.0f)) return 1.0f;
    f32 k = 1.0f;
    while (k * 0.5f >= onScreenScale && k > 1.0f / 32.0f) k *= 0.5f;
    return k;
}

/// Cena do modelo (glTF: metros, Y para cima, +Z para o observador) → espaço
/// da layer (pixels, Y para baixo, Z para dentro): giro de 180° em X (troca Y
/// e Z sem espelhar — a face da frente continua da frente), escala de
/// metros para pixels e o pivô no centro da caixa do modelo.
Mat4 layer_from_model(const Model3DData& m) noexcept {
    Mat4 flip;
    flip.col[1] = Vec4{0, -1, 0, 0};
    flip.col[2] = Vec4{0, 0, -1, 0};
    return flip * Mat4::scale(Vec3{m.unitScale, m.unitScale, m.unitScale}) * Mat4::translation(-m.pivot);
}

Mat4 layer_matrix(const Layer& l, FrameIndex local) noexcept {
    const TrackSet& t = l.tracks;
    auto s = [&](TrackProperty p, f32 fallback) noexcept {
        const Track* tr = t.find(p);
        return tr ? tr->value_or(local, fallback) : fallback;
    };
    const Vec3 pos{s(TrackProperty::PositionX, l.transform.position.x),
                   s(TrackProperty::PositionY, l.transform.position.y), 0.0f};
    const Vec3 scale{s(TrackProperty::ScaleX, l.transform.scale.x),
                     s(TrackProperty::ScaleY, l.transform.scale.y), 1.0f};
    const f32 rot = s(TrackProperty::RotationZ, l.transform.rotation.z) * kDeg2Rad;
    const Vec3 anchor{s(TrackProperty::AnchorX, l.transform.anchor.x),
                      s(TrackProperty::AnchorY, l.transform.anchor.y), 0.0f};
    return Mat4::translation(pos) * Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, rot))
         * Mat4::scale(scale) * Mat4::translation(-anchor);
}

/// Curves, holds and expressions all evaluate at the actual exposure time.
f32 sample_frac(const Track& tr, f64 t, f32 fallback) noexcept {
    return tr.value_or_f(t, fallback);
}

/// `layer_matrix` num tempo local fracionário. Os animadores de camada (entrada,
/// saída, wiggle) somam por cima do transform — é por aqui que preview e export
/// os veem iguais.
Mat4 layer_matrix_frac(const Layer& l, f64 local, f64 fps) noexcept {
    const TrackSet& t = l.tracks;
    auto s = [&](TrackProperty p, f32 fallback) noexcept {
        const Track* tr = t.find(p);
        return tr ? sample_frac(*tr, local, fallback) : fallback;
    };
    const layeranim::Offset an = layeranim::whole_offset(l, local, fps);
    const Vec3 pos{s(TrackProperty::PositionX, l.transform.position.x) + an.translate.x,
                   s(TrackProperty::PositionY, l.transform.position.y) + an.translate.y, 0.0f};
    const Vec3 scale{s(TrackProperty::ScaleX, l.transform.scale.x) * an.scale.x,
                     s(TrackProperty::ScaleY, l.transform.scale.y) * an.scale.y, 1.0f};
    const f32 rot = (s(TrackProperty::RotationZ, l.transform.rotation.z) + an.rotation.z) * kDeg2Rad;
    const Vec3 anchor{s(TrackProperty::AnchorX, l.transform.anchor.x),
                      s(TrackProperty::AnchorY, l.transform.anchor.y), 0.0f};
    return Mat4::translation(pos) * Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, rot))
         * Mat4::scale(scale) * Mat4::translation(-anchor);
}

/// Transform 3D completo da layer (posição/rotação/escala/âncora em XYZ),
/// num tempo local fracionário (inteiro = o próprio quadro).
Mat4 layer_matrix_3d_frac(const Layer& l, f64 local, f64 fps) noexcept {
    const TrackSet& t = l.tracks;
    auto s = [&](TrackProperty p, f32 fallback) noexcept {
        const Track* tr = t.find(p);
        return tr ? sample_frac(*tr, local, fallback) : fallback;
    };
    const layeranim::Offset an = layeranim::whole_offset(l, local, fps);
    const Vec3 pos{s(TrackProperty::PositionX, l.transform.position.x) + an.translate.x,
                   s(TrackProperty::PositionY, l.transform.position.y) + an.translate.y,
                   s(TrackProperty::PositionZ, l.transform.position.z)};
    Vec3 scale{s(TrackProperty::ScaleX, l.transform.scale.x) * an.scale.x, s(TrackProperty::ScaleY, l.transform.scale.y) * an.scale.y,
               s(TrackProperty::ScaleZ, l.transform.scale.z)};
    // A profundidade acompanha a largura (Z multiplica X) em tudo que não é
    // câmera/luz: os controles de escala da UI mexem só em X/Y. Sem isto,
    // escalar um modelo — ou o NULO pai dele — o achataria em profundidade
    // (perfil e sombreamento esticam em vez de dar zoom).
    if (l.kind != LayerKind::Camera && l.kind != LayerKind::Light) scale.z *= scale.x;
    const Vec3 rot{(s(TrackProperty::RotationX, l.transform.rotation.x) + an.rotation.x) * kDeg2Rad,
                   (s(TrackProperty::RotationY, l.transform.rotation.y) + an.rotation.y) * kDeg2Rad,
                   (s(TrackProperty::RotationZ, l.transform.rotation.z) + an.rotation.z) * kDeg2Rad};
    const Vec3 anchor{s(TrackProperty::AnchorX, l.transform.anchor.x), s(TrackProperty::AnchorY, l.transform.anchor.y),
                      s(TrackProperty::AnchorZ, l.transform.anchor.z)};
    return Mat4::translation(pos) * Mat4::from_quat(Quat::from_euler_zyx(rot.x, rot.y, rot.z)) * Mat4::scale(scale)
         * Mat4::translation(-anchor);
}

f32 layer_opacity(const Layer& l, FrameIndex local, f64 fps) noexcept {
    const Track* tr = l.tracks.find(TrackProperty::Opacity);
    const f32 v = tr ? tr->value_or(local, l.transform.opacity) : l.transform.opacity;
    const f32 an = layeranim::has_whole(l) ? layeranim::whole_offset(l, static_cast<f64>(local.value), fps).opacity : 1.0f;
    return std::clamp(v * an, 0.0f, 1.0f);
}

/// Matriz clip ← composição: pixels da composição (y para baixo) → NDC do
/// Vulkan (y para baixo também, então só escala e desloca).
Mat4 clip_from_comp(f32 w, f32 h) noexcept {
    Mat4 m;
    m.col[0] = Vec4{2.0f / w, 0, 0, 0};
    m.col[1] = Vec4{0, 2.0f / h, 0, 0};
    m.col[3] = Vec4{-1.0f, -1.0f, 0, 1};
    return m;
}

struct LayerPush {
    Mat4 clipFromLayer;
    Vec4 region;
    Vec4 uvRect;
    Vec4 params;
};
static_assert(sizeof(LayerPush) <= binding::kPushConstantBytes, "push constants do layer.vert");

struct YuvUniforms {
    Vec4 coeffs;
    Vec4 transfer;
    Vec4 crop;
    Vec4 rot;
    Vec4 sampling;
    Vec4 texel;
};

/// Composição ← clip (inversa de clip_from_comp).
Mat4 comp_from_clip(f32 w, f32 h) noexcept {
    Mat4 m;
    m.col[0] = Vec4{w * 0.5f, 0, 0, 0};
    m.col[1] = Vec4{0, h * 0.5f, 0, 0};
    m.col[3] = Vec4{w * 0.5f, h * 0.5f, 0, 1};
    return m;
}

/// A camada (ou algum pai) vive no espaço 3D? Marcada como 3D, ou com rotação
/// em X/Y ou profundidade no instante — aí a perspectiva é a da câmera da
/// cena, não um "achatado" 2D (rotação X/Y num 2D não mudava nada na tela).
bool under_layering_camera(const Composition& comp, const Layer& l, FrameIndex time) noexcept {
    switch (l.kind) {
        case LayerKind::Video: case LayerKind::Image: case LayerKind::Text:
        case LayerKind::Shape: case LayerKind::Composition: break;
        default: return false;
    }
    const auto& order = comp.order();
    for (u32 n = order.size(); n-- > 0;) {
        const Layer* camera = comp.layer(order.at(n));
        if (!camera) continue;
        if (camera == &l) return false;
        if (camera->kind == LayerKind::Camera && camera->camera.active && camera->camera.affectsLayersBelow
            && camera->visible && camera->contains_time(time)) return true;
    }
    return false;
}

bool wants_3d(const Composition& comp, const Layer& l, FrameIndex time) noexcept {
    if (under_layering_camera(comp, l, time)) return true;
    const Layer* p = &l;
    for (u32 depth = 0; p && depth < 17; ++depth) {
        if (p->threeD) return true;
        const FrameIndex local = p->local_time(time);
        auto s = [&](TrackProperty prop, f32 fallback) noexcept {
            const Track* tr = p->tracks.find(prop);
            return tr ? tr->value_or(local, fallback) : fallback;
        };
        if (s(TrackProperty::RotationX, p->transform.rotation.x) != 0.0f
            || s(TrackProperty::RotationY, p->transform.rotation.y) != 0.0f
            || s(TrackProperty::PositionZ, p->transform.position.z) != 0.0f) {
            return true;
        }
        // Animador de camada girando em X/Y (entrada "de pé", "virando"): 3D.
        if (layeranim::has_whole(*p)) {
            const layeranim::Offset an = layeranim::whole_offset(*p, static_cast<f64>(local.value), comp.fps() > 0.0 ? comp.fps() : 30.0);
            if (an.rotation.x != 0.0f || an.rotation.y != 0.0f) return true;
        }
        p = p->parent.valid() ? comp.layer(p->parent) : nullptr;
    }
    return false;
}

// Parenting Helper changes the inherited basis, while the child's anchor
// keeps following the parent's full transform (e.g. a Ferris-wheel gondola).
Mat4 parenting_basis(const Layer& child, Mat4 parent, f64 local, f32* continuousZ) noexcept {
    static const ParameterRegistry parameters = [] {
        ParameterRegistry p;
        p.add_float("rotation", "Inherit Rotation", 100.f, -200.f, 200.f);
        p.add_float("scale", "Inherit Scale", 100.f, 0.f, 200.f);
        return p;
    }();
    for (const EffectInstance& effect : child.effects) {
        if (!effect.enabled || effect.type != effect_type_id(effect_keys::kParentingHelper)) continue;
        auto value = [&](u32 i) {
            return evaluate_param_f(child.tracks, effect, i, parameters.at(i), local).as_float() * .01f;
        };
        const f32 rotation = value(0), scale = value(1);
        if (rotation == 1.f && scale == 1.f) continue;
        // QR keeps shear and signed scale when only rotation is changed.
        auto xyz = [](Vec4 v) { return Vec3{v.x, v.y, v.z}; };
        const Vec3 a = xyz(parent.col[0]), b = xyz(parent.col[1]), c = xyz(parent.col[2]);
        const f32 sx = a.length();
        const Vec3 x = sx > 1e-6f ? a * (1.f / sx) : Vec3{1, 0, 0};
        const f32 xy = x.dot(b);
        const Vec3 perpendicular = b - x * xy;
        const f32 sy = perpendicular.length();
        const Vec3 fallback = std::abs(x.y) < .9f ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
        const Vec3 y = sy > 1e-6f ? perpendicular * (1.f / sy) : (fallback - x * x.dot(fallback)).normalized();
        const Vec3 z = x.cross(y).normalized();
        const f32 sz = z.dot(c), xz = x.dot(c), yz = y.dot(c);
        const f32 ry = std::asin(std::clamp(-x.z, -1.f, 1.f));
        const bool pole = std::abs(std::cos(ry)) < 1e-5f;
        const f32 rx = pole ? 0.f : std::atan2(y.z, z.z);
        f32 rz = pole ? std::atan2(-y.x, y.y) : std::atan2(x.y, x.x);
        // A 2D parent can turn past 180 degrees or make several revolutions.
        // Matrix decomposition alone loses those turns and makes 50% weights jump.
        if (continuousZ) {
            rz += std::round((*continuousZ - rz) / (2.f * kPi)) * (2.f * kPi);
            *continuousZ = rz * rotation;
        }
        const Mat4 r = Mat4::from_quat(Quat::from_euler_zyx(rx * rotation, ry * rotation, rz * rotation));
        Mat4 stretch;
        stretch.col[0] = {lerpf(1.f, sx, scale), 0, 0, 0};
        stretch.col[1] = {xy * scale, lerpf(1.f, sy, scale), 0, 0};
        stretch.col[2] = {xz * scale, yz * scale, lerpf(1.f, sz, scale), 0};
        const Vec4 translation = parent.col[3];
        parent = r * stretch;
        parent.col[3] = translation;
    }
    return parent;
}

Mat4 world_chain_frac(const Composition& comp, const Layer& l, f64 time, bool threeD, f64 parentTime) noexcept {
    const Layer* chain[17]{};
    u32 count = 0;
    for (const Layer* p = &l; p && count < 17; p = p->parent.valid() ? comp.layer(p->parent) : nullptr) {
        // Projects normally reject cycles on edit/load; retain a finite fallback.
        bool cycle = false;
        for (u32 i = 0; i < count; ++i) cycle |= chain[i] == p;
        if (cycle) break;
        chain[count++] = p;
    }
    auto ownMatrix = [&](const Layer& node) {
        const f64 local = (&node == &l ? time : parentTime) - static_cast<f64>(node.start.value) + static_cast<f64>(node.offset.value);
        const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
        const Mat4 own = threeD ? layer_matrix_3d_frac(node, local, fps) : layer_matrix_frac(node, local, fps);
        const Mat4 bound = node.hasParentBasis ? node.parentBasis * own : own;
        return grid::evaluate(comp,node,&node == &l ? time : parentTime).matrix * bound;
    };
    bool helper = false;
    for (u32 i = 0; i + 1 < count; ++i)
        for (const auto& effect : chain[i]->effects)
            helper |= effect.enabled && effect.type == effect_type_id(effect_keys::kParentingHelper);
    if (!helper) {
        Mat4 plain = ownMatrix(l);
        for (u32 i = 1; i < count; ++i) plain = ownMatrix(*chain[i]) * plain;
        return plain;
    }
    Mat4 world;
    f32 continuousZ = 0.f;
    while (count) {
        const Layer& node = *chain[--count];
        const f64 local = (&node == &l ? time : parentTime) - static_cast<f64>(node.start.value) + static_cast<f64>(node.offset.value);
        const Mat4 own = ownMatrix(node);
        const Mat4 basis = parenting_basis(node, world, local, threeD ? nullptr : &continuousZ);
        auto anchor = [&](TrackProperty property, f32 fallback) {
            const Track* track = node.tracks.find(property);
            return track ? sample_frac(*track, local, fallback) : fallback;
        };
        const Vec4 pivot{anchor(TrackProperty::AnchorX, node.transform.anchor.x),
                         anchor(TrackProperty::AnchorY, node.transform.anchor.y),
                         threeD ? anchor(TrackProperty::AnchorZ, node.transform.anchor.z) : 0.f, 1.f};
        const Vec4 wanted = world * (own * pivot);
        world = basis * own;
        const Vec4 actual = world * pivot;
        world.col[3].x += wanted.x - actual.x;
        world.col[3].y += wanted.y - actual.y;
        world.col[3].z += wanted.z - actual.z;
        if (!threeD) continuousZ += anchor(TrackProperty::RotationZ, node.transform.rotation.z) * kDeg2Rad;
    }
    return world;
}

Mat4 world_2d_frac(const Composition& comp, const Layer& l, f64 time) noexcept {
    return world_chain_frac(comp, l, time, false, time);
}
Mat4 world_3d_frac(const Composition& comp, const Layer& l, f64 time) noexcept {
    return world_chain_frac(comp, l, time, true, time);
}

/// Mundo 3D da camada com a cadeia de pais (cada pai no próprio tempo).
Mat4 world_3d(const Composition& comp, const Layer& l, FrameIndex time) noexcept {
    return world_3d_frac(comp, l, static_cast<f64>(time.value));
}

/// Modelo 3D no instante (fracionário): mundo com a cadeia de pais e pose da
/// animação no relógio da TIMELINE (tempo local × velocidade do clipe).
void place_model(const Composition& comp, const Layer& l, const scene3d::SceneAsset& asset, f64 time,
                 scene3d::SceneInstance& inst, scene3d::PoseWorkspace* workspace = nullptr) {
    inst.world = world_3d_frac(comp, l, time) * layer_from_model(l.model);
    inst.materials = l.model.materials;
    const f64 materialTime = time - static_cast<f64>(l.start.value) + static_cast<f64>(l.offset.value);
    for (u32 i = 0; i < l.tracks.size(); ++i) {
        const auto& track = l.tracks.at(i);
        if (track.property != TrackProperty::MaterialParam || track.effectIndex >= asset.materials.size() || track.effectParamIndex >= 6) continue;
        MaterialOverride* target = nullptr;
        for (auto& over : inst.materials) if (over.materialIndex == track.effectIndex) { target = &over; break; }
        if (!target) {
            const auto& source = asset.materials[track.effectIndex];
            MaterialOverride over; over.materialIndex = track.effectIndex; over.baseColor = source.baseColor; over.metallic = source.metallic; over.roughness = source.roughness;
            inst.materials.push_back(over); target = &inst.materials.back();
        }
        f32* field = track.effectParamIndex == 0 ? &target->baseColor.x : track.effectParamIndex == 1 ? &target->baseColor.y : track.effectParamIndex == 2 ? &target->baseColor.z
                   : track.effectParamIndex == 3 ? &target->baseColor.w : track.effectParamIndex == 4 ? &target->metallic : &target->roughness;
        *field = std::clamp(sample_frac(track, materialTime, *field), 0.0f, 1.0f);
        target->mask |= 1u << track.effectParamIndex;
    }
    scene3d::Pose pose;
    const i32 clip = l.model.animationClip;
    f32 t = 0.0f;
    if (clip >= 0 && clip < static_cast<i32>(asset.animations.size())) {
        const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
        const f64 local = time - static_cast<f64>(l.start.value) + static_cast<f64>(l.offset.value);
        t = scene3d::clip_time(asset.animations[static_cast<usize>(clip)], local / fps * l.model.timeScale);
    }
    // Rig posado pelo usuário (trilhas BonePose): entra antes das juntas.
    thread_local std::vector<scene3d::BoneOffset> boneOffsets;
    scene3d::bone_offsets(l, materialTime, boneOffsets);
    scene3d::evaluate_pose(asset, clip, t, pose, workspace, boneOffsets.data(), boneOffsets.size());
    scene3d::apply_text3d_layout(asset, l, materialTime, pose.nodeWorld);
    // Formas 3D: o layout das partes (Shape 3D Layout) antes das trilhas de cada parte.
    scene3d::apply_shape3d_layout(asset, l, materialTime, pose.nodeWorld);
    // Formas 3D: cada parte com o transform das trilhas dela (mesmo caminho no export).
    scene3d::apply_shape3d_parts(asset, l, materialTime, pose.nodeWorld);
    // Animação de texto 3D (animadores por letra/palavra/linha): mesmo caminho no export.
    scene3d::apply_text3d_animators(asset, l, materialTime, comp.fps(), pose.nodeWorld, inst.nodeOpacity, &inst.nodeFill);
    // Animar parte 3D / Esconder partes 3D: por último (os animadores refazem a opacidade).
    scene3d::apply_part_effects(asset, l, materialTime, pose.nodeWorld, inst.nodeOpacity);
    inst.nodeWorld = std::move(pose.nodeWorld);
    inst.jointMatrices = std::move(pose.jointMatrices);
    inst.skinJointOffset = std::move(pose.skinJointOffset);
    inst.morphWeights = std::move(pose.morphWeights);
}

// Layer transforms, camera, shadows and environment can change independently
// of an immutable model's bind pose. Deformers and material tracks still take
// the evaluated path at their exact timeline time, including export samples.
bool can_share_static_pose(const Layer& layer, const scene3d::SceneAsset& asset) {
    if (layer.model.animationClip >= 0 && layer.model.animationClip < static_cast<i32>(asset.animations.size())) return false;
    if (!layer.model.materials.empty()) return false;
    if (scene3d::has_part_effects(layer)) return false;
    if (scene3d::has_bone_pose(layer)) return false;
    for (u32 i = 0; i < layer.tracks.size(); ++i) {
        const auto property = layer.tracks.at(i).property;
        if (property == TrackProperty::MaterialParam || (asset.shapeParts && property == TrackProperty::ShapePart)) return false;
    }
    if (!asset.textGlyphLayout && !asset.shapeParts) return true;
    if (layeranim::has_units(layer) || text::has_animator_effect(layer)) return false;
    if (asset.textGlyphLayout && (text::has_transform_effect(layer) || text::has_animators(layer.text))) return false;
    for (const auto& effect : layer.effects) {
        if (effect.enabled && ((asset.textGlyphLayout && effect.type == effect_type_id(effect_keys::kText3DLayout)) ||
            (asset.shapeParts && effect.type == effect_type_id(effect_keys::kShape3DLayout)))) return false;
    }
    return true;
}

/// FOV vertical (graus) da câmera no instante local: a trilha de distância
/// focal (mm) manda quando tem keyframe — uma zoom 24→85 mm interpola em mm,
/// como uma lente de verdade, e a projeção sai de FOV = 2·atan(24 / 2f);
/// senão a trilha de FOV; senão o valor parado (sincronizado com o mm).
f32 camera_fov_frac(const Layer& l, f64 local) noexcept {
    if (const Track* focal = l.tracks.find(TrackProperty::FocalLength); focal && focal->driven())
        return camera_fov_from_focal(sample_frac(*focal, local, l.camera.focalLength));
    const Track* fov = l.tracks.find(TrackProperty::Fov);
    return fov ? sample_frac(*fov, local, l.camera.fov) : l.camera.fov;
}

/// Grupo que deixa a câmera de fora atravessar (CompositionRef::cameraPassThrough):
/// enquanto a composição de DENTRO é preparada, a câmera dela é a de fora,
/// levada para o espaço do grupo (vista × mundo do grupo). Vale só durante o
/// `prepare` da filha (salvo e restaurado em volta dele).
struct CameraPassThrough {
    const Composition* child = nullptr;
    const Composition* parent = nullptr;
    const Layer* group = nullptr;
    f64 parentTime0 = 0.0, childTime0 = 0.0, rate = 1.0;   ///< tempo de fora = t0 + (tempo de dentro − c0) × rate
};
thread_local CameraPassThrough g_cameraPassThrough;

/// A camada existe no instante FRACIONÁRIO? As amostras do obturador vão até
/// ±meio quadro em volta do quadro desenhado: no 1º quadro da câmera metade
/// delas caía no quadro −1, a câmera "sumia" e entrava a câmera padrão —
/// a cópia fantasma parada quando a câmera tem pai (beta 12/10, câmera presa
/// a um nulo). Um quadro de folga nas bordas mantém a mesma câmera.
bool contains_time_frac(const Layer& l, f64 timeF) noexcept {
    const i64 t = static_cast<i64>(std::floor(timeF));
    if (l.contains_time(FrameIndex{t})) return true;
    return l.end.value > l.start.value && (t == l.start.value - 1 || t == l.end.value);
}

bool has_active_camera_frac(const Composition& comp, f64 timeF) noexcept {
    const auto& order = comp.order();
    for (u32 i = 0; i < order.size(); ++i) {
        const Layer* l = comp.layer(order.at(i));
        if (l && l->kind == LayerKind::Camera && l->camera.active && l->visible && contains_time_frac(*l, timeF)) return true;
    }
    return false;
}

bool has_active_camera(const Composition& comp, FrameIndex time) noexcept {
    const auto& order = comp.order();
    for (u32 i = 0; i < order.size(); ++i) {
        const Layer* l = comp.layer(order.at(i));
        if (l && l->kind == LayerKind::Camera && l->camera.active && l->visible && l->contains_time(time)) return true;
    }
    return false;
}

// Projected particles are rasterized back into the source plane. Keep this
// inverse separate from effects appended later to compFromLayer, so those
// effects still transform the generated pixels exactly once.
bool inverse_particle_plane(const Mat4& matrix, Mat4& inverse) noexcept {
    const tracking::Homography plane{{matrix.col[0].x, matrix.col[1].x, matrix.col[3].x,
        matrix.col[0].y, matrix.col[1].y, matrix.col[3].y,
        matrix.col[0].w, matrix.col[1].w, matrix.col[3].w}};
    tracking::Homography result;
    if (!plane.inverse(result)) return false;
    const auto& h = result.m;
    inverse.col[0] = Vec4{static_cast<f32>(h[0]), static_cast<f32>(h[3]), 0, static_cast<f32>(h[6])};
    inverse.col[1] = Vec4{static_cast<f32>(h[1]), static_cast<f32>(h[4]), 0, static_cast<f32>(h[7])};
    inverse.col[2] = Vec4{0, 0, 1, 0};
    inverse.col[3] = Vec4{static_cast<f32>(h[2]), static_cast<f32>(h[5]), 0, static_cast<f32>(h[8])};
    return true;
}

/// Inversa de uma matriz afim (3×3 + translação); singular = identidade.
Mat4 affine_inverse(const Mat4& m) noexcept {
    const f32 a = m.col[0].x, b = m.col[1].x, c = m.col[2].x;
    const f32 d = m.col[0].y, e = m.col[1].y, f = m.col[2].y;
    const f32 g = m.col[0].z, h = m.col[1].z, k = m.col[2].z;
    const f32 A = e * k - f * h, B = -(d * k - f * g), C = d * h - e * g;
    const f32 det = a * A + b * B + c * C;
    if (std::fabs(det) < 1e-12f) return Mat4::identity();
    const f32 inv = 1.0f / det;
    Mat4 r;
    r.col[0] = Vec4{A * inv, B * inv, C * inv, 0};
    r.col[1] = Vec4{-(b * k - c * h) * inv, (a * k - c * g) * inv, -(a * h - b * g) * inv, 0};
    r.col[2] = Vec4{(b * f - c * e) * inv, -(a * f - c * d) * inv, (a * e - b * d) * inv, 0};
    const Vec3 t{m.col[3].x, m.col[3].y, m.col[3].z};
    const Vec3 rt = r.transform_point(t);
    r.col[3] = Vec4{-rt.x, -rt.y, -rt.z, 1};
    return r;
}

scene3d::SceneCamera camera_for_frac_own(const Composition& comp, f64 timeF, u32 w, u32 h) noexcept;

/// Câmera da composição no instante: a camada de câmera ATIVA visível; sem
/// ela, a padrão (plano Z=0 em escala 1:1 com a composição). Dentro de um
/// grupo que a câmera atravessa, a câmera de fora.
scene3d::SceneCamera camera_for_frac(const Composition& comp, f64 timeF, u32 w, u32 h) noexcept {
    const CameraPassThrough pt = g_cameraPassThrough;
    if (pt.child != &comp || !pt.parent || !pt.group) return camera_for_frac_own(comp, timeF, w, h);
    const f64 outer = pt.parentTime0 + (timeF - pt.childTime0) * pt.rate;
    if (!has_active_camera_frac(*pt.parent, outer))
        return camera_for_frac_own(comp, timeF, w, h);
    g_cameraPassThrough = CameraPassThrough{};   // a de fora com as regras dela
    scene3d::SceneCamera cam = camera_for_frac(*pt.parent, outer, w, h);
    g_cameraPassThrough = pt;
    const Mat4 group = world_3d_frac(*pt.parent, *pt.group, outer);
    cam.view = cam.view * group;
    cam.position = affine_inverse(group).transform_point(cam.position);
    return cam;
}

scene3d::SceneCamera camera_for_frac_own(const Composition& comp, f64 timeF, u32 w, u32 h) noexcept {
    scene3d::SceneCamera cam = scene3d::default_camera(w, h);
    const OrderedIds<LayerId>& order = comp.order();
    for (u32 i = 0; i < order.size(); ++i) {
        const Layer* l = comp.layer(order.at(i));
        if (!l || !l->visible || !contains_time_frac(*l, timeF)) continue;
        if (l->kind != LayerKind::Camera || !l->camera.active) continue;
        const f64 local = timeF - static_cast<f64>(l->start.value) + static_cast<f64>(l->offset.value);
        const Mat4 wm = world_3d_frac(comp, *l, timeF);
        const Vec3 right{wm.col[0].x, wm.col[0].y, wm.col[0].z};
        const Vec3 up{wm.col[1].x, wm.col[1].y, wm.col[1].z};
        Vec3 z = Vec3{wm.col[2].x, wm.col[2].y, wm.col[2].z}.normalized();
        // Parent scale followed by child rotation can shear the world basis.
        // A camera keeps its transformed forward/up directions, but its view
        // must be rigid: independently normalizing the axes still distorts it.
        if (z.length_sq() < kEpsilon) z = right.cross(up).normalized();
        if (z.length_sq() < kEpsilon) z = Vec3{0, 0, 1};
        Vec3 x = up.cross(z).normalized();
        if (x.length_sq() < kEpsilon) x = (right - z * right.dot(z)).normalized();
        if (x.length_sq() < kEpsilon)
            x = (std::fabs(z.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}).cross(z).normalized();
        const Vec3 y = z.cross(x).normalized();
        const Vec3 t{wm.col[3].x, wm.col[3].y, wm.col[3].z};
        Mat4 v;
        v.col[0] = Vec4{x.x, y.x, z.x, 0};
        v.col[1] = Vec4{x.y, y.y, z.y, 0};
        v.col[2] = Vec4{x.z, y.z, z.z, 0};
        v.col[3] = Vec4{-x.dot(t), -y.dot(t), -z.dot(t), 1};
        cam.view = v;
        cam.position = t;
        cam.fovY = std::clamp(camera_fov_frac(*l, local), 1.0f, 170.0f) * kDeg2Rad;
        cam.nearZ = std::max(0.1f, l->camera.nearPlane);
        // Lente: profundidade de campo no instante (preview e export iguais).
        if (l->camera.dofEnabled) {
            auto lens = [&](TrackProperty p, f32 fallback) {
                const Track* t = l->tracks.find(p);
                const f32 v = t ? sample_frac(*t, local, fallback) : fallback;
                return std::isfinite(v) ? v : fallback;
            };
            cam.dof = true;
            cam.focusDistance = std::max(0.01f, lens(TrackProperty::FocusDistance, l->camera.focusDistance));
            cam.fStop = std::clamp(lens(TrackProperty::Aperture, l->camera.aperture), 0.5f, 64.0f);
            cam.blurAmount = std::clamp(lens(TrackProperty::CameraBlur, l->camera.blurAmount), 0.0f, 4.0f);
            cam.pixelsPerMeter = scene3d::scene_pixels_per_meter(comp.height());
        }
        const Vec2 sourceSize = l->camera.trackingSourceSize;
        const Layer* source = comp.layer(l->cameraTrackSource);
        if (source && sourceSize.x > 0 && sourceSize.y > 0) {
            // Reframe the solved source's calibrated image into composition
            // pixels. Includes translation, nonuniform scale and Z rotation;
            // all 3D paths (meshes, planes, particles, hit testing) share it.
            Mat4 aspectCorrection = Mat4::identity();
            aspectCorrection.col[0].x = (static_cast<f32>(w) / std::max(1u, h)) / (sourceSize.x / sourceSize.y);
            cam.imageTransform = clip_from_comp(static_cast<f32>(w), static_cast<f32>(h))
                * world_2d_frac(comp, *source, timeF) * comp_from_clip(sourceSize.x, sourceSize.y) * aspectCorrection;
        }
        break;
    }
    return cam;
}

scene3d::SceneCamera camera_for(const Composition& comp, FrameIndex time, u32 w, u32 h) noexcept {
    return camera_for_frac(comp, static_cast<f64>(time.value), w, h);
}

/// Composição (px) ← mundo, com a câmera no instante fracionário.
Mat4 comp_view_projection_frac(const Composition& comp, f64 time, u32 w, u32 h) noexcept {
    const scene3d::SceneCamera cam = camera_for_frac(comp, time, w, h);
    return comp_from_clip(static_cast<f32>(w), static_cast<f32>(h))
         * cam.imageTransform * scene3d::reverse_z_perspective(cam.fovY, static_cast<f32>(w) / static_cast<f32>(std::max(1u, h)), cam.nearZ) * cam.view;
}

} // namespace

f32 camera_fov_deg_at(const Layer& camera, f64 localFrame) noexcept { return camera_fov_frac(camera, localFrame); }
scene3d::SceneCamera comp_camera(const Composition& comp, FrameIndex time, u32 w, u32 h) noexcept { return camera_for(comp, time, w, h); }

namespace {
/// Raio × caixa (slab): entra antes de `limit`?
bool ray_hits_box(Vec3 o, Vec3 d, const scene3d::Aabb& b, f32 limit) noexcept {
    if (!b.valid()) return false;
    f32 t0 = 0.0f, t1 = limit;
    const f32 oo[3]{o.x, o.y, o.z}, dd[3]{d.x, d.y, d.z};
    const f32 lo[3]{b.min.x, b.min.y, b.min.z}, hi[3]{b.max.x, b.max.y, b.max.z};
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(dd[a]) < 1e-12f) {
            if (oo[a] < lo[a] || oo[a] > hi[a]) return false;
            continue;
        }
        f32 n = (lo[a] - oo[a]) / dd[a], f = (hi[a] - oo[a]) / dd[a];
        if (n > f) std::swap(n, f);
        t0 = std::max(t0, n);
        t1 = std::min(t1, f);
        if (t0 > t1) return false;
    }
    return true;
}
/// Möller-Trumbore, dos dois lados (a face de trás também serve ao foco).
bool ray_hits_triangle(Vec3 o, Vec3 d, Vec3 a, Vec3 b, Vec3 c, f32& t) noexcept {
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 p = d.cross(e2);
    const f32 det = e1.dot(p);
    if (std::fabs(det) < 1e-12f) return false;
    const f32 inv = 1.0f / det;
    const Vec3 s = o - a;
    const f32 u = s.dot(p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const Vec3 q = s.cross(e1);
    const f32 v = d.dot(q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    t = e2.dot(q) * inv;
    return t > 0.0f;
}
} // namespace

bool scene_ray(const scene3d::SceneCamera& cam, u32 width, u32 height, f32 compX, f32 compY,
               Vec3& origin, Vec3& dir) noexcept {
    if (!std::isfinite(compX) || !std::isfinite(compY)) return false;
    const u32 w = std::max(1u, width), h = std::max(1u, height);
    // px da composição → clip, desfazendo o enquadramento da fonte rastreada
    // (afim no plano x/y do clip; identidade numa câmera comum).
    f32 nx = 2.0f * compX / static_cast<f32>(w) - 1.0f, ny = 2.0f * compY / static_cast<f32>(h) - 1.0f;
    {
        const Mat4& m = cam.imageTransform;
        const f32 a = m.col[0].x, b = m.col[1].x, c = m.col[0].y, d = m.col[1].y;
        const f32 det = a * d - b * c;
        if (std::fabs(det) < 1e-9f) return false;
        const f32 px = nx - m.col[3].x, py = ny - m.col[3].y;
        nx = (d * px - b * py) / det;
        ny = (-c * px + a * py) / det;
    }
    const f32 aspect = static_cast<f32>(w) / static_cast<f32>(h);
    const f32 t = std::tan(cam.fovY * 0.5f);
    const Mat4& v = cam.view;
    const Vec3 ax{v.col[0].x, v.col[1].x, v.col[2].x}, ay{v.col[0].y, v.col[1].y, v.col[2].y},
               az{v.col[0].z, v.col[1].z, v.col[2].z};
    dir = (ax * (nx * aspect * t) + ay * (ny * t) + az).normalized();
    origin = cam.position;
    return std::isfinite(dir.x) && std::isfinite(dir.y) && std::isfinite(dir.z);
}

bool ray_hits_model_layer(const Composition& comp, const Layer& l, FrameIndex time, const scene3d::SceneAsset& asset,
                          Vec3 origin, Vec3 dir, f32& best) noexcept {
    scene3d::SceneInstance inst;
    place_model(comp, l, asset, static_cast<f64>(time.value), inst);
    bool any = false;
    std::vector<Vec3> pts;
    for (usize n = 0; n < asset.nodes.size(); ++n) {
        const scene3d::Node& node = asset.nodes[n];
        if (!node.inScene) continue;
        if (node.mesh < 0 || static_cast<usize>(node.mesh) >= asset.meshes.size()) continue;
        const Mat4 m = inst.world * (n < inst.nodeWorld.size() ? inst.nodeWorld[n] : Mat4::identity());
        const bool skinNode = node.skin >= 0 && static_cast<usize>(node.skin) < inst.skinJointOffset.size();
        for (const scene3d::Primitive& prim : asset.meshes[static_cast<usize>(node.mesh)].primitives) {
            const bool sk = skinNode && prim.skinned() && prim.joints.size() >= prim.positions.size() * 4
                         && prim.weights.size() >= prim.positions.size();
            if (!sk && !ray_hits_box(origin, dir, prim.bounds.transformed(m), best)) continue;
            // Pose do quadro: skin na CPU (as mesmas matrizes do render);
            // morph fica na forma base (aproximação do foco).
            pts.resize(prim.positions.size());
            const usize first = sk ? inst.skinJointOffset[static_cast<usize>(node.skin)] : 0;
            for (usize k = 0; k < prim.positions.size(); ++k) {
                const Vec3 pos = prim.positions[k];
                if (!sk) { pts[k] = m.transform_point(pos); continue; }
                const Vec4 wt = prim.weights[k];
                const f32 ws[4]{wt.x, wt.y, wt.z, wt.w};
                Vec3 acc{0, 0, 0};
                for (usize j = 0; j < 4; ++j) {
                    const usize joint = first + prim.joints[k * 4 + j];
                    if (ws[j] <= 0.0f || joint >= inst.jointMatrices.size()) continue;
                    acc = acc + inst.jointMatrices[joint].transform_point(pos) * ws[j];
                }
                pts[k] = inst.world.transform_point(acc);
            }
            const usize tris = prim.indices.size() / 3;
            for (usize k = 0; k < tris; ++k) {
                const u32 i0 = prim.indices[k * 3], i1 = prim.indices[k * 3 + 1], i2 = prim.indices[k * 3 + 2];
                if (i0 >= pts.size() || i1 >= pts.size() || i2 >= pts.size()) continue;
                f32 hit = 0.0f;
                if (ray_hits_triangle(origin, dir, pts[i0], pts[i1], pts[i2], hit) && hit < best) { best = hit; any = true; }
            }
        }
    }
    return any;
}

bool pick_scene_point(const Composition& comp, FrameIndex time, f32 compX, f32 compY,
                      std::shared_ptr<const scene3d::SceneAsset> (*lookup)(void* ctx, AssetId id), void* ctx,
                      Vec3& outWorld) noexcept {
    if (!lookup) return false;
    const u32 w = std::max(1u, comp.width()), h = std::max(1u, comp.height());
    Vec3 origin, dir;
    if (!scene_ray(camera_for(comp, time, w, h), w, h, compX, compY, origin, dir)) return false;
    f32 best = std::numeric_limits<f32>::max();
    const OrderedIds<LayerId>& order = comp.order();
    for (u32 i = 0; i < order.size(); ++i) {
        const Layer* l = comp.layer(order.at(i));
        if (!l || !l->visible || !l->contains_time(time) || l->kind != LayerKind::Model3D) continue;
        const std::shared_ptr<const scene3d::SceneAsset> asset = lookup(ctx, l->model.scene);
        if (asset) (void)ray_hits_model_layer(comp, *l, time, *asset, origin, dir, best);
    }
    if (!(best < std::numeric_limits<f32>::max())) return false;
    outWorld = origin + dir * best;
    return true;
}

scene3d::SceneCamera scene_editor_camera(u32 width, u32 height, const SceneEditorView& editor) noexcept {
    scene3d::SceneCamera cam = scene3d::default_camera(width, height);
    const f32 yaw = editor.yaw * kDeg2Rad, pitch = editor.pitch * kDeg2Rad;
    const f32 distance = static_cast<f32>(std::max(1u, height)) * editor.distance;
    const Vec3 target{width * 0.5f, height * 0.5f, 0};
    const Vec3 position = target + Vec3{std::sin(yaw) * std::cos(pitch) * distance,
        -std::sin(pitch) * distance, -std::cos(yaw) * std::cos(pitch) * distance};
    const Vec3 z = (target - position).normalized();
    const Vec3 x = Vec3{0, 1, 0}.cross(z).normalized();
    const Vec3 y = z.cross(x).normalized();
    cam.position = position;
    cam.view.col[0] = Vec4{x.x, y.x, z.x, 0};
    cam.view.col[1] = Vec4{x.y, y.y, z.y, 0};
    cam.view.col[2] = Vec4{x.z, y.z, z.z, 0};
    cam.view.col[3] = Vec4{-x.dot(position), -y.dot(position), -z.dot(position), 1};
    return cam;
}

Mat4 scene_editor_projection(u32 width, u32 height, const SceneEditorView& editor) noexcept {
    const auto cam = scene_editor_camera(width, height, editor);
    return comp_from_clip(static_cast<f32>(width), static_cast<f32>(height))
        * cam.imageTransform * scene3d::reverse_z_perspective(cam.fovY, static_cast<f32>(width) / std::max(1u, height), cam.nearZ) * cam.view;
}

Mat4 comp_view_projection(const Composition& comp, FrameIndex time) noexcept {
    const u32 w = std::max(1u, comp.width()), h = std::max(1u, comp.height());
    const scene3d::SceneCamera cam = camera_for(comp, time, w, h);
    return comp_from_clip(static_cast<f32>(w), static_cast<f32>(h))
         * cam.imageTransform * scene3d::reverse_z_perspective(cam.fovY, static_cast<f32>(w) / static_cast<f32>(h), cam.nearZ) * cam.view;
}

Mat4 layer_world_3d(const Composition& comp, const Layer& l, FrameIndex time) noexcept { return world_3d(comp, l, time); }
bool wants_layer_3d(const Composition& comp, const Layer& l, FrameIndex time) noexcept { return wants_3d(comp, l, time); }

// Partículas (8.2, ParticleScene.cpp): as MESMAS contas de mundo e câmera,
// num instante fracionário da timeline.
Mat4 particle_world_3d_at(const Composition& comp, const Layer& l, f64 time, f64 parentTime) noexcept {
    return world_chain_frac(comp, l, time, true, parentTime);
}
Mat4 particle_world_2d_at(const Composition& comp, const Layer& l, f64 time, f64 parentTime) noexcept {
    return world_chain_frac(comp, l, time, false, parentTime);
}
scene3d::SceneCamera particle_camera_at(const Composition& comp, f64 time, u32 w, u32 h) noexcept {
    return camera_for_frac(comp, time, w, h);
}

Mat4 layer_comp_matrix(const Composition& comp, const Layer& l, FrameIndex time, bool* perspective) noexcept {
    const bool is3d = wants_3d(comp, l, time);
    if (perspective) *perspective = is3d;
    if (!is3d) return layer_world_matrix(comp, l, time);
    const u32 w = std::max(1u, comp.width()), h = std::max(1u, comp.height());
    const scene3d::SceneCamera cam = camera_for(comp, time, w, h);
    return comp_from_clip(static_cast<f32>(w), static_cast<f32>(h))
         * cam.imageTransform * scene3d::reverse_z_perspective(cam.fovY, static_cast<f32>(w) / static_cast<f32>(h), cam.nearZ) * cam.view
         * world_3d(comp, l, time);
}

Mat4 layer_world_matrix(const Composition& comp, const Layer& l, FrameIndex time) noexcept {
    if (wants_3d(comp, l, time)) return world_3d(comp, l, time);
    return world_2d_frac(comp, l, static_cast<f64>(time.value));
}

// Sistemas pesados (8E): o export é sempre cheio; o preview segue o bloco
// explícito ou a escada da escala única.
HeavyQuality resolve_heavy(const RenderSettings& s) noexcept {
    if (s.finalQuality) {
        HeavyQuality q = HeavyQuality::full();
        q.exportFrame = true;
        return q;
    }
    if (s.heavyExplicit) return s.heavy;
    const u32 den = std::max(1u, s.previewDenominator) / std::max(1u, s.previewNumerator);
    return HeavyQuality::from_scale(s.heavyScale, den);
}

// =============================================================================
// Ciclo de vida
// =============================================================================
std::shared_ptr<const scene3d::SceneInstance> Renderer::static_model_pose(const std::shared_ptr<const scene3d::SceneAsset>& asset) {
    constexpr u64 budget = 8ull * 1024 * 1024;
    auto found = staticPoses_.find(asset.get());
    if (found != staticPoses_.end() && !found->second.owner.expired()) {
        found->second.used = ++staticPoseUse_;
        return found->second.pose;
    }
    // Weak owners avoid retaining an imported model's geometry and textures
    // after deletion. Address reuse can never return another asset's pose.
    for (auto it = staticPoses_.begin(); it != staticPoses_.end();) {
        if (it->second.owner.expired()) { staticPoseBytes_ -= it->second.bytes; it = staticPoses_.erase(it); }
        else ++it;
    }
    u64 bytes = sizeof(scene3d::SceneInstance) + asset->nodes.size() * (sizeof(Mat4) + sizeof(std::vector<f32>));
    for (const auto& node : asset->nodes) bytes += node.morphWeights.size() * sizeof(f32);
    for (const auto& skin : asset->skins) bytes += sizeof(u32) + skin.joints.size() * sizeof(Mat4);
    if (bytes > budget) return nullptr;
    auto evict = [&] {
        const auto oldest = std::min_element(staticPoses_.begin(), staticPoses_.end(),
            [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
        staticPoseBytes_ -= oldest->second.bytes; staticPoses_.erase(oldest);
    };
    while (!staticPoses_.empty() && (staticPoseBytes_ + bytes > budget || staticPoses_.size() >= 64)) evict();
    scene3d::Pose evaluated;
    scene3d::evaluate_pose(*asset, -1, 0, evaluated, &poseWorkspace_);
    auto pose = std::make_shared<scene3d::SceneInstance>();
    pose->nodeWorld = std::move(evaluated.nodeWorld);
    pose->jointMatrices = std::move(evaluated.jointMatrices);
    pose->skinJointOffset = std::move(evaluated.skinJointOffset);
    pose->morphWeights = std::move(evaluated.morphWeights);
    bytes = sizeof(scene3d::SceneInstance) + pose->nodeWorld.capacity() * sizeof(Mat4)
        + pose->jointMatrices.capacity() * sizeof(Mat4) + pose->skinJointOffset.capacity() * sizeof(u32)
        + pose->morphWeights.capacity() * sizeof(std::vector<f32>);
    for (const auto& weights : pose->morphWeights) bytes += weights.capacity() * sizeof(f32);
    if (bytes > budget) return nullptr;
    while (!staticPoses_.empty() && staticPoseBytes_ + bytes > budget) evict();
    staticPoses_.emplace(asset.get(), StaticPoseEntry{asset, pose, bytes, ++staticPoseUse_});
    staticPoseBytes_ += bytes;
    return pose;
}

Renderer::Renderer() {
    draws_.reserve(64);
    framesInFlight_.reserve(16);
    timingScratch_.resize(1024);   // um por passe: 50 camadas com efeitos passam de 190
}

Renderer::~Renderer() { shutdown(); }

Status Renderer::initialize(GPUBackend& backend, const EffectRegistry& effects) noexcept {
    backend_ = &backend;
    failedGraphFrame_ = 0;
    effects_ = &effects;
    // Tipos que o prepare consulta por camada: resolvidos uma vez (a busca
    // por chave é linear no registro).
    posterizeType_ = effects.find_key(effect_keys::kPosterizeTime);
    lowFpsType_ = effects.find_key(effect_keys::kLowFps);
    randomEditsType_ = effects.find_key(effect_keys::kRandomEdits);
    echoType_ = effects.find_key(effect_keys::kEchoTrail);
    rgbTimeType_ = effects.find_key(effect_keys::kTimeWarpRgb);
    motionDetectType_ = effects.find_key(effect_keys::kMotionDetect);
    datamoshType_ = effects.find_key(effect_keys::kDatamosh);
    timeSliceType_ = effects.find_key(effect_keys::kTimeSlice);
    wideTimeType_ = effects.find_key(effect_keys::kWideTime);
    if (const Status s = shaders_.initialize(backend); !s.ok()) {
        shaders_.shutdown();
        backend_ = nullptr;
        return s;
    }

    // PRÉ-AQUECIMENTO DA ABERTURA (Fase 8I, §60): só o que TODO projeto usa
    // — vídeo, forma, vetor, texto, máscara, pilha de cor, composição e
    // saída. Efeitos e 3D (antes: todos os ~40 pipelines aqui, pagos em toda
    // abertura mesmo sem projeto) são compilados quando o projeto aberto os
    // tem (scan_for_warmup), fora do playback. O 3D sobe só as texturas
    // neutras de 1 px; os pipelines dele esperam a primeira camada 3D.
    if (const Status s = scene3d_.initialize(backend, shaders_); !s.ok()) {
        AUREA_LOG_ERROR("renderer: 3D indisponivel: %s", s.message().data());
    }
    warmPending_.clear();
    warmedEffects_.clear();
    warmSceneQualities_ = warmedSceneSamples_ = 0;
    warmedForProject_ = 0;
    std::vector<PipelineKey> keys;
    keys.push_back(PipelineKey::fullscreen(ShaderId::effects_color_stack_frag, kWorkFormat));
    keys.push_back(PipelineKey::graphics(ShaderId::text_glyph_vert, ShaderId::text_glyph_frag, kWorkFormat, true,
                                         BlendMode::Normal));
    keys.push_back(PipelineKey::fullscreen(ShaderId::video_yuv_planar_frag, kWorkFormat));
    keys.push_back(PipelineKey::fullscreen(ShaderId::video_rgba_to_linear_frag, kWorkFormat));
    keys.push_back(PipelineKey::fullscreen(ShaderId::shape_shape_frag, kWorkFormat));
    keys.push_back(PipelineKey::graphics(ShaderId::vector_path_vert, ShaderId::vector_path_frag, kWorkFormat, true, BlendMode::Normal));
    keys.push_back(PipelineKey::graphics(ShaderId::rig_mesh_vert, ShaderId::rig_mesh_frag, kWorkFormat, true, BlendMode::Normal));
    keys.push_back(PipelineKey::fullscreen(ShaderId::common_copy_frag, kWorkFormat));
    keys.push_back(PipelineKey::fullscreen(ShaderId::common_copy_frag, kPreviewCacheFormat));
    keys.push_back(PipelineKey::fullscreen(ShaderId::mask_raster_frag, SurfaceFormat::R8));
    keys.push_back(PipelineKey::fullscreen(ShaderId::mask_apply_frag, kWorkFormat));
    keys.push_back(PipelineKey::fullscreen(ShaderId::mask_track_matte_frag, kWorkFormat));
    keys.push_back(PipelineKey::graphics(ShaderId::composite_layer_vert, ShaderId::composite_layer_frag,
                                         kWorkFormat, true, BlendMode::Normal));
    keys.push_back(PipelineKey::graphics(ShaderId::composite_layer_vert, ShaderId::composite_layer_frag,
                                         kWorkFormat, true, BlendMode::Add));
    // Modos que leem o fundo: cópia do fundo (sem blend) + quad com blend.frag.
    keys.push_back(PipelineKey::graphics(ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat));
    keys.push_back(PipelineKey::graphics(ShaderId::composite_layer_vert, ShaderId::composite_blend_frag, kWorkFormat));
    for (SurfaceFormat f : {SurfaceFormat::RGBA8, SurfaceFormat::BGRA8}) {
        keys.push_back(PipelineKey::graphics(ShaderId::composite_layer_vert, ShaderId::composite_output_frag, f));
    }
    // Export (planos Y em R8 e CbCr em RG8) não entra: compila no primeiro
    // quadro do export, que não é tempo real.
    prewarmed_ = shaders_.prewarm(keys.data(), static_cast<u32>(keys.size()));
    AUREA_LOG_INFO("renderer: %u pipelines pre-aquecidos", prewarmed_);
    return OkStatus;
}

void Renderer::scan_for_warmup(const Project& project) noexcept {
    if (!effects_ || !shaders_.ready()) return;
    // Sem alocação no caso comum: tudo já visto → nenhuma inserção.
    warmSceneQualities_ = 0;
    project.timeline().for_each_composition([&](CompositionId, const Composition& c) {
        bool want3d = c.environment().showBackground
            && (c.environment().hdri.valid() || c.environment().studioPreset != 0);
        c.layers().for_each([&](LayerId, const Layer& l) {
            if (l.threeD || l.kind == LayerKind::Model3D) want3d = true;
            for (const EffectInstance& e : l.effects) {
                if (!e.enabled) continue;
                const auto it = std::lower_bound(warmedEffects_.begin(), warmedEffects_.end(), e.type);
                if (it != warmedEffects_.end() && *it == e.type) continue;
                warmedEffects_.insert(it, e.type);
                if (const Effect* fx = effects_->find(e.type)) fx->pipelines(warmPending_, kWorkFormat);
            }
        });
        if (want3d) warmSceneQualities_ |= 1u << std::min(c.post_process().quality3d, kScene3DQualityCount - 1);
    });
}

void Renderer::warm_scene_pipelines(const FrameSnapshot& snapshot) noexcept {
    // The project scan runs before render quality has been resolved. Keep its
    // composition qualities, then derive the SAME sample count as the draw.
    // Actual snapshots also cover a direct play/quality change before a paused
    // scan, and nested compositions have independent quality settings.
    const auto visit = [&](auto&& self, const FrameSnapshot& frame) -> void {
        for (const auto& group : frame.scenes)
            warmSceneQualities_ |= 1u << std::min(group.post.quality, kScene3DQualityCount - 1);
        for (const auto& child : frame.nested) if (child) self(self, *child);
    };
    visit(visit, snapshot);
    const auto warmQuality = [&](const HeavyQuality& quality) {
        scene3d_.set_post_quality(quality.msaaSamples, quality.fxaa, quality.bloomStartDiv, quality.bloomLevels);
        const u32 samples = scene3d_.pass_samples();
        const u32 bit = 1u << samples;
        if (warmedSceneSamples_ & bit) return;
        scene3d_.collect_pipelines(warmPending_);
        warmedSceneSamples_ |= bit;
    };
    for (u32 tier = 0; tier < kScene3DQualityCount; ++tier) {
        if (!(warmSceneQualities_ & (1u << tier))) continue;
        HeavyQuality quality = heavyQ_;
        apply_scene3d_quality(quality, static_cast<Scene3DQuality>(tier));
        warmQuality(quality);
        if (tier == static_cast<u32>(Scene3DQuality::Auto) && !heavyQ_.exportFrame) {
            // Auto can move between exactly these two profiles as load/heat
            // changes. Compile both before its first playback, never the
            // unrelated Low (1x) or Ultra export (8x) profiles.
            for (const u32 samples : {2u, 4u}) {
                quality.msaaSamples = samples;
                warmQuality(quality);
            }
        }
    }
}

void Renderer::flush_warmup() noexcept {
    if (warmPending_.empty()) return;
    // Só o que ainda não existe: vários efeitos dividem pipelines.
    const u32 made = shaders_.prewarm(warmPending_.data(), static_cast<u32>(warmPending_.size()));
    warmedForProject_ += made;
    if (made) AUREA_LOG_INFO("renderer: %u pipelines do projeto pre-aquecidos", made);
    warmPending_.clear();
}

void Renderer::shutdown() noexcept {
    shutdown_impl(false);
}

void Renderer::shutdown_after_gpu_done() noexcept {
    shutdown_impl(true);
}

void Renderer::shutdown_impl(bool gpuDone) noexcept {
    cancel_preview_graph();
    staticPoses_.clear(); staticPoseBytes_ = 0; poseWorkspace_ = {};
    if (!backend_) return;
    if (!gpuDone) backend_->wait_idle();
    framesInFlight_.clear();
    release_project_resources_impl(gpuDone);
    if (glyphAtlas_.valid()) backend_->destroy_texture(glyphAtlas_);
    glyphAtlas_ = TextureHandle{};
    if (particleQuad_.valid()) backend_->destroy_buffer(particleQuad_);
    particleQuad_ = BufferHandle{};
    if (previewSrcTex_.valid()) backend_->destroy_texture(previewSrcTex_);
    previewSrcTex_ = TextureHandle{};
    previewSrcDirty_ = !previewSrc_.empty();
    for (u32 i = 0; i < kGlyphRing; ++i) {
        if (glyphBuf_[i].valid()) backend_->destroy_buffer(glyphBuf_[i]);
        glyphBuf_[i] = BufferHandle{};
        glyphCap_[i] = 0;
        if (maskBuf_[i].valid()) backend_->destroy_buffer(maskBuf_[i]);
        maskBuf_[i] = BufferHandle{};
        maskCap_[i] = 0;
        if (vecBuf_[i].valid()) backend_->destroy_buffer(vecBuf_[i]);
        vecBuf_[i] = BufferHandle{};
        vecCap_[i] = 0;
        if (particleBuf_[i].valid()) backend_->destroy_buffer(particleBuf_[i]);
        particleBuf_[i] = BufferHandle{};
        particleCap_[i] = 0;
    }
    vectorCache_.clear();
    scene3d_.shutdown();
    pool_.clear();
    failedGraphFrame_ = 0;
    shaders_.shutdown();
    backend_ = nullptr;
}

void Renderer::forget_device() noexcept {
    cancel_preview_graph();
    staticPoses_.clear(); staticPoseBytes_ = 0; poseWorkspace_ = {};
    previewFrames_.clear();
    previewCacheKey_ = 0;
    previewCacheCapacity_ = 0;
    previewCacheRevision_ = previewCacheComposition_ = 0;
    previewCacheHit_ = false;
    publish_preview_buffer_ranges();
    glyphAtlas_ = TextureHandle{};
    particleQuad_ = BufferHandle{};
    glyphAtlasGen_ = 0;
    for (u32 i = 0; i < kGlyphRing; ++i) {
        glyphBuf_[i] = BufferHandle{}; glyphCap_[i] = 0;
        maskBuf_[i] = BufferHandle{}; maskCap_[i] = 0;
        vecBuf_[i] = BufferHandle{}; vecCap_[i] = 0;
        particleBuf_[i] = BufferHandle{}; particleCap_[i] = 0;
    }
    maskCache_.clear();
    vecFrameBuf_ = BufferHandle{};
    particleFrameBuf_ = BufferHandle{};
    framesInFlight_.clear();
    planar_.clear();
    release_slice_cache(false);   // dispositivo perdido: os handles não valem mais
    flowCache_.clear();
    heavyStats_.flowCacheBytes = 0;
    images_.clear();
    sceneImages_.clear();
    luts_.clear();
    spectra_.clear();
    depthTex_.clear();
    rotoRefinedTex_.clear();
    depthState_.clear();
    uploads_.clear();
    particleExtraBufs_.forget();
    particleStatics_.clear();
    scene3d_.forget_device();
    pool_.forget();
    failedGraphFrame_ = 0;
    shaders_.forget_device();
    backend_ = nullptr;
}

void Renderer::release_project_resources() noexcept {
    release_project_resources_impl(false);
}

void Renderer::release_project_resources_impl(bool gpuDone) noexcept {
    cancel_preview_graph();
    staticPoses_.clear(); staticPoseBytes_ = 0; poseWorkspace_ = {};
    warmSceneQualities_ = 0;
    renderInstancePath_.clear();
    nestedNamespaces_.clear();
    nestSalt_ = 0;
    signatures_.clear();   // assinaturas de duplicados: eram das fontes do projeto
    if (!backend_) return;
    incomplete_ = false;
    // Called between frames under the engine's render mutex. Complete the old
    // project's submissions before releasing their imported images/buffers.
    if (!gpuDone) backend_->wait_idle();
    clear_preview_cache();
    // Imported native buffers are backend caches, outside planar_/images_.
    // Leaving them behind retained entire closed decoder pools after reload.
    // The export renderer shares the backend import cache with preview. Its
    // certified teardown releases only its own resources; a global import
    // trim could invalidate another renderer's retained handles.
    if (!gpuDone) (void)backend_->trim_external_images();
    pool_.clear();
    failedGraphFrame_ = 0;
    for (auto& [k, p] : planar_) {
        for (TextureHandle& t : p.plane) if (t.valid()) backend_->destroy_texture(t);
    }
    for (auto& [k, i] : images_) {
        destroy_image_linear(i);
        backend_->destroy_texture(i.texture);
    }
    for (auto& [k, l] : luts_) backend_->destroy_texture(l.texture);
    for (auto& [k, i] : sceneImages_) backend_->destroy_texture(i.texture);
    sceneImages_.clear();
    for (auto& [k, s] : spectra_) backend_->destroy_texture(s.texture);
    for (auto& [k, f] : flowCache_) for (TextureHandle& t : f.tex) if (t.valid()) backend_->destroy_texture(t);
    flowCache_.clear();
    heavyStats_.flowCacheBytes = 0;
    for (auto& [k, m] : maskCache_) for (TextureHandle& t : m.tex) if (t.valid()) backend_->destroy_texture(t);
    maskCache_.clear();
    release_slice_cache(true);
    planar_.clear();
    images_.clear();
    luts_.clear();
    spectra_.clear();
    // Projeto fechado: os blocos de som eram dele. Os mapas de profundidade também.
    spectrumBlocks_.reset();
    ownAudio_.reset();
    audioViz_.clear();
    audioPeaks_.clear();
    release_depth(true);
    spectrumFactory_ = nullptr;
    uploads_.clear();
    particleExtraBufs_.release(*backend_);
    particleStatics_.clear();
    scene3d_.release_all();
    // Backends may defer destruction even after the first wait. Drain those
    // releases now, so the next project does not overlap the old one's VRAM.
    if (!gpuDone) backend_->wait_idle();
}

void Renderer::clear_preview_cache() noexcept {
    cancel_preview_graph();
    previewCacheRevision_ = previewCacheComposition_ = 0;
    // Withdraw the UI snapshot before releasing any GPU resources.
    publish_preview_buffer_ranges();
    if (backend_) for (auto& frame : previewFrames_)
        if (frame.texture.valid()) backend_->destroy_texture(frame.texture);
    previewFrames_.clear();
    previewCacheKey_ = 0;
    previewCacheCapacity_ = 0;
    previewCacheHit_ = false;
}

u32 Renderer::release_preview_cache() noexcept {
    u32 n = 0;
    for (const auto& frame : previewFrames_) if (frame.texture.valid()) ++n;
    clear_preview_cache();
    return n;
}

void Renderer::set_low_memory_device(bool low) noexcept {
    lowMemoryDevice_ = low;
    if (roto_) roto_->set_max_bytes(low ? ai::RotoService::kLowMemoryMaxBytes : ai::RotoService::kMaxBytes);
}

void Renderer::set_preview_cache_budget(u64 bytes) noexcept {
    // O teto vem da faixa de RAM (preview_cache_budget); 1 GiB só impede um
    // valor absurdo. Crescer não descarta nada; encolher (pressão) libera já.
    const u64 bounded = std::min<u64>(bytes, 1ull << 30);
    if (bounded < previewCacheBudget_) clear_preview_cache();
    previewCacheBudget_ = bounded;
}

void Renderer::invalidate_preview_frames(i64 start, i64 end) noexcept {
    // Edição local: só os quadros em [start, end) deixam de valer; o resto do
    // cache segue servindo. O slot volta a ser o primeiro da fila de despejo.
    bool changed = false;
    for (auto& f : previewFrames_) {
        if (f.time >= start && f.time < end) { f.complete = false; f.time = -1; f.used = 0; changed = true; }
    }
    if (changed) publish_preview_buffer_ranges();
}

u32 Renderer::configure_preview_cache(u32 width, u32 height, const RenderSettings& s) noexcept {
    if (!s.previewCacheRevision || s.finalQuality || s.rawPlayback || !backend_) {
        clear_preview_cache();
        return 0;
    }
    const u32 num = std::max(1u, s.previewNumerator), den = std::max(1u, s.previewDenominator);
    const u32 w = std::max(1u, width * num / den), h = std::max(1u, height * num / den);
    u64 key = 1469598103934665603ull;
    auto add = [&](u64 value) { key = (key ^ value) * 1099511628211ull; };
    auto number = [&](f32 value) { add(std::bit_cast<u32>(value)); };
    add(s.previewCacheRevision); add(s.previewCacheComposition); add(w); add(h);
    add(rigSetupLayer_); add(s.sceneEditor.enabled);
    number(s.sceneEditor.yaw); number(s.sceneEditor.pitch); number(s.sceneEditor.distance);
    number(s.heavyScale);
    const PreviewQuality q = effective_quality(s);
    number(q.motionBlurSamples); number(q.flowResolution); number(q.ssao);
    number(q.shadowResolution); number(q.particles); number(q.blurSamples); number(q.lodBias);
    const HeavyQuality heavy = resolve_heavy(s);
    number(heavy.particles); number(heavy.flow); number(heavy.lodBias); number(heavy.effects);
    add(heavy.flowBlurSamples); add(heavy.shadowMapSize); add(heavy.shadowFilter);
    add(heavy.msaaSamples); add(heavy.fxaa); add(heavy.bloomStartDiv); add(heavy.bloomLevels);
    if (key != previewCacheKey_) { clear_preview_cache(); previewCacheKey_ = key; }
    previewCacheRevision_ = s.previewCacheRevision;
    previewCacheComposition_ = s.previewCacheComposition;
    previewCacheCapacity_ = preview_cache_capacity(w, h, previewCacheBudget_, kPreviewCacheBytesPerPixel);
    return previewCacheCapacity_;
}

bool Renderer::preview_cached(FrameIndex time) const noexcept {
    for (const auto& f : previewFrames_) if (f.time == time.value && f.complete) return true;
    return false;
}

u32 Renderer::preview_cached_count() const noexcept {
    return static_cast<u32>(std::count_if(previewFrames_.begin(), previewFrames_.end(),
        [](const auto& f) { return f.complete; }));
}

void Renderer::publish_preview_buffer_ranges() noexcept {
    // Only the render thread reads previewFrames_. Sorting/grouping is done
    // outside the UI mutex; publication copies a fixed, tiny snapshot.
    std::array<i64, kPreviewCacheMaxFrames> frames{};
    u32 frameCount = 0;
    if (previewCacheRevision_) for (const auto& frame : previewFrames_) {
        if (frame.complete && frame.texture.valid() && frame.time >= 0
            && frame.time < std::numeric_limits<i64>::max() && frameCount < frames.size())
            frames[frameCount++] = frame.time;
    }
    std::sort(frames.begin(), frames.begin() + frameCount);
    std::array<i64, kPreviewCacheMaxFrames * 2> ranges{};
    u32 rangeCount = 0;
    for (u32 i = 0; i < frameCount; ++i) {
        const i64 frame = frames[i];
        if (rangeCount && frame <= ranges[rangeCount * 2 - 1])
            ranges[rangeCount * 2 - 1] = std::max(ranges[rangeCount * 2 - 1], frame + 1);
        else {
            ranges[rangeCount * 2] = frame;
            ranges[rangeCount * 2 + 1] = frame + 1;
            ++rangeCount;
        }
    }
    std::lock_guard<std::mutex> lock(previewRangesMutex_);
    previewRangePairs_ = ranges;
    previewRangeCount_ = rangeCount;
    previewRangeRevision_ = previewCacheRevision_;
    previewRangeComposition_ = previewCacheComposition_;
}

u32 Renderer::copy_preview_buffer_ranges(u64 revision, u64 composition,
                                         i64* outPairs, u32 capacityRanges) const noexcept {
    if (!outPairs || !capacityRanges || !revision) return 0;
    std::lock_guard<std::mutex> lock(previewRangesMutex_);
    if (revision != previewRangeRevision_ || composition != previewRangeComposition_) return 0;
    const u32 count = std::min(capacityRanges, previewRangeCount_);
    std::copy_n(previewRangePairs_.begin(), count * 2, outPairs);
    return count;
}

PreviewQuality Renderer::effective_quality(const RenderSettings& s) noexcept {
    if (s.finalQuality) return PreviewQuality{};   // export: nada do preview entra
    PreviewQuality q = s.quality;
    const f32 h = std::clamp(s.heavyScale, 0.0f, 1.0f);
    q.motionBlurSamples = std::min(q.motionBlurSamples, h);
    q.flowResolution = std::min(q.flowResolution, h);
    q.ssao = std::min(q.ssao, h);
    q.shadowResolution = std::min(q.shadowResolution, h);
    q.particles = std::min(q.particles, h);
    q.blurSamples = std::min(q.blurSamples, h);
    return q;
}

// =============================================================================
// EffectResources
// =============================================================================
TextureHandle Renderer::curve_lut(const CurveData& curve) noexcept {
    if (!backend_) return TextureHandle{};
    const u64 key = curve.hash();
    if (auto it = luts_.find(key); it != luts_.end()) {
        it->second.lastFrame = frameNumber_;
        return it->second.texture;
    }
    TextureDesc d;
    d.width = 256;
    d.height = 1;
    d.format = SurfaceFormat::RGBA16F;
    d.sampled = true;
    d.transferDst = true;
    d.debugName = "lut-curvas";
    auto tex = backend_->create_texture(d);
    if (!tex.ok()) return TextureHandle{};

    // Canal = mestra ∘ canal (a mestra age sobre R, G e B, depois cada canal).
    PendingUpload up;
    up.texture = *tex;
    up.bytesPerRow = 256 * 8;
    up.data.resize(256 * 8);
    u16* px = reinterpret_cast<u16*>(up.data.data());
    for (u32 i = 0; i < 256; ++i) {
        const f32 x = static_cast<f32>(i) / 255.0f;
        const f32 m = std::clamp(curve.evaluate(0, x), 0.0f, 1.0f);
        px[i * 4 + 0] = to_half(std::clamp(curve.evaluate(1, m), 0.0f, 1.0f));
        px[i * 4 + 1] = to_half(std::clamp(curve.evaluate(2, m), 0.0f, 1.0f));
        px[i * 4 + 2] = to_half(std::clamp(curve.evaluate(3, m), 0.0f, 1.0f));
        px[i * 4 + 3] = to_half(1.0f);
    }
    uploads_.push_back(std::move(up));
    luts_[key] = LutTexture{*tex, frameNumber_};
    return *tex;
}

TextureHandle Renderer::cube_lut(AssetId id) noexcept {
    if (!backend_ || !cubeLookup_) return {};
    const u64 key = id.pack() ^ 0x435542455f4c5554ull;
    if (auto it = luts_.find(key); it != luts_.end()) {
        it->second.lastFrame = frameNumber_;
        return it->second.texture;
    }
    const auto cube = cubeLookup_(cubeCtx_, id);
    if (!cube || cube->values.empty()) return {};
    TextureDesc desc;
    desc.width = std::min<u32>(1024u, static_cast<u32>(cube->values.size()));
    desc.height = static_cast<u32>((cube->values.size() + desc.width - 1) / desc.width);
    desc.format = SurfaceFormat::RGBA16F; desc.sampled = true; desc.transferDst = true;
    desc.debugName = "cube-lut";
    auto tex = backend_->create_texture(desc); if (!tex.ok()) return {};
    PendingUpload up; up.texture = *tex; up.bytesPerRow = desc.width * 8;
    up.data.resize(usize(desc.width) * desc.height * 8);
    auto* target = reinterpret_cast<u16*>(up.data.data());
    for (usize i = 0; i < cube->values.size(); ++i) {
        const Vec4 v = cube->values[i];
        target[i*4] = to_half(v.x); target[i*4+1] = to_half(v.y);
        target[i*4+2] = to_half(v.z); target[i*4+3] = to_half(1);
    }
    uploads_.push_back(std::move(up)); luts_[key] = LutTexture{*tex, frameNumber_};
    return *tex;
}

// Malha de deformação: grade tesselada em px da camada (float cheio — meia
// precisão erraria 2 px numa camada de 4000).
TextureHandle Renderer::data_texture(u64 key, const Vec4* texels, u32 width, u32 height) noexcept {
    if (!backend_ || !texels || width == 0 || height == 0 || width > 4096 || height > 4096) return {};
    key ^= 0x4d4553485f574152ull;
    if (auto it = luts_.find(key); it != luts_.end()) {
        it->second.lastFrame = frameNumber_;
        return it->second.texture;
    }
    TextureDesc d;
    d.width = width;
    d.height = height;
    d.format = SurfaceFormat::RGBA32F;
    d.sampled = true;
    d.transferDst = true;
    d.debugName = "malha-deformacao";
    auto tex = backend_->create_texture(d);
    if (!tex.ok()) return {};
    PendingUpload up;
    up.texture = *tex;
    up.bytesPerRow = width * 16;
    up.data.resize(usize(width) * height * 16);
    std::memcpy(up.data.data(), texels, up.data.size());
    uploads_.push_back(std::move(up));
    luts_[key] = LutTexture{*tex, frameNumber_};
    // Malha animada = uma grade por quadro: só as últimas ficam (a faxina de
    // 240 quadros das LUTs guardaria dezenas de MB na reprodução).
    meshDataKeys_.push_back(key);
    for (usize i = 0; meshDataKeys_.size() > 12 && i < meshDataKeys_.size();) {
        auto it = luts_.find(meshDataKeys_[i]);
        if (it == luts_.end()) { meshDataKeys_.erase(meshDataKeys_.begin() + static_cast<std::ptrdiff_t>(i)); continue; }
        if (it->second.lastFrame + 2 >= frameNumber_) { ++i; continue; }
        backend_->destroy_texture(it->second.texture);
        luts_.erase(it);
        meshDataKeys_.erase(meshDataKeys_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return *tex;
}

namespace {

/// A camada tem som que toca no instante `t`? (vídeo com trilha ou áudio,
/// dentro do intervalo, não congelada)
bool layer_sounds_at(const Layer& l, const Project& project, FrameIndex t) noexcept {
    if (l.kind != LayerKind::Video && l.kind != LayerKind::Audio) return false;
    if (t.value < l.start.value || t.value >= l.end.value) return false;
    const Asset* a = project.asset(l.source);
    if (!a || !a->has_audio()) return false;
    return l.speed > 0.0f || (l.timeRemapEnabled && !l.timeRemap.keys.empty());
}

} // namespace

std::vector<Vec4> Renderer::repeat_path(const Layer* host, u32 count, f32 phase, u64 guideId) noexcept {
    std::vector<Vec4> result;
    if (!planComp_ || !host || count == 0) return result;
    const auto& comp=*planComp_;
    const auto& order=comp.order(); const Layer* guide=nullptr;
    if (guideId) guide = comp.layer(LayerId::unpack(guideId));
    else for(u32 i=1;i<order.size();++i) if(comp.layer(order.at(i))==host) { guide=comp.layer(order.at(i-1)); break; }
    if (guide == host) return result;
    if(!guide) return result;
    vector::Contour contour;
    if(guide->kind==LayerKind::Shape) {
        const auto& shape=guide->shape;
        const Vec2 center{shape.bounds.x+shape.bounds.w*.5f,shape.bounds.y+shape.bounds.h*.5f};
        const Vec2 size{shape.bounds.w,shape.bounds.h};
        if(shape.shapeType==kShapeVector) {
            if(!vector::guide_contour(shape.vector,guide->tracks,double(guide->local_time(planTime_).value),contour))return result;
        } else if(shape.shapeType==0)vector::flatten(vector::make_rect(center,size,shape.cornerRadius),.25f,contour);
        else if(shape.shapeType==1)vector::flatten(vector::make_ellipse(center,size),.25f,contour);
        else if(shape.shapeType==2){contour.pts=shape.path;contour.closed=shape.filled;}
    }
    if(contour.pts.empty()&&!guide->masks.empty()) {
        std::vector<MaskPoint> points;std::vector<Vec4> edges;
        const auto& mask=guide->masks.front();
        mask::evaluate_path(mask,double(guide->local_time(planTime_).value),points);
        mask::flatten(points,mask.closed,.25f,{},edges);
        contour.closed=mask.closed;
        for(const auto& edge:edges)contour.pts.push_back({edge.x,edge.y});
        if(!mask.closed&&!edges.empty())contour.pts.push_back({edges.back().z,edges.back().w});
    }
    auto affine=[](const Mat4& m){return vector::Affine2{m.col[0].x,m.col[0].y,m.col[1].x,m.col[1].y,m.col[3].x,m.col[3].y};};
    // Camada de ajuste: o plano dela é o da composição (os pixels são os da
    // pilha abaixo), não o do sólido que a representa na timeline.
    const Mat4 hostMatrix = host->adjustment ? Mat4::identity() : layer_world_matrix(comp,*host,planTime_);
    const auto toHost=affine(hostMatrix).inverse()*affine(layer_world_matrix(comp,*guide,planTime_));
    for(auto& point:contour.pts)point=toHost.apply(point);
    const double length=vector::length_of(contour); if(length<1e-6)return result;
    // Teto de 2048 amostras: a Constelação põe até 2000 pontos no caminho; a
    // Repetição (64 cópias) e o Mover no caminho (1) já pedem menos que isso.
    count=std::min(count,2048u); result.reserve(count);
    for(u32 i=0;i<count;++i) {
        double unit=double(i)/double(contour.closed?count:std::max(1u,count-1))+phase;
        unit=contour.closed?unit-std::floor(unit):std::clamp(unit,0.,1.);
        Vec2 point,tangent; if(!vector::sample_at(contour,unit*length,point,tangent))return {};
        result.push_back({point.x,point.y,std::atan2(tangent.y,tangent.x),0});
    }
    return result;
}

DepthMapResult Renderer::depth_map_below(const DepthMapRequest& request) noexcept {
    DepthMapResult none;
    none.failed = false;
    if (!planComp_ || !request.host) return none;
    const Composition& comp = *planComp_;
    const Layer& adj = *request.host;
    const OrderedIds<LayerId>& order = comp.order();
    u32 at = order.size();
    for (u32 i = 0; i < order.size(); ++i) if (comp.layer(order.at(i)) == &adj) { at = i; break; }
    // A de cima entre as que ela ajusta: a logo abaixo (escopo 1), uma das
    // escolhidas (escopo 2) ou a foto/vídeo mais alta abaixo (escopo 0).
    const Layer* src = nullptr;
    for (u32 i = at; i-- > 0;) {
        const Layer* c = comp.layer(order.at(i));
        if (!c || !c->visible || !c->contains_time(planTime_) || c->adjustment || (c->guide && planFinal_)) continue;
        if (adj.adjustmentScope == 2 && std::find(adj.adjustmentTargets.begin(), adj.adjustmentTargets.end(), order.at(i))
                                            == adj.adjustmentTargets.end()) continue;
        if (c->kind == LayerKind::Image || c->kind == LayerKind::Video) { src = c; break; }
        if (adj.adjustmentScope == 1) break;
    }
    // Camada 3D: a projeção não é afim; o efeito devolve o quadro intacto.
    if (!src || wants_3d(comp, *src, planTime_)) return none;
    u32 w = 0, h = 0;
    if (src->kind == LayerKind::Image) {
        const ImagePixels* px = planImageLookup_ ? planImageLookup_(planImageCtx_, src->source) : nullptr;
        if (px) { w = px->width; h = px->height; }
    } else if (const Asset* asset = planProject_ ? planProject_->asset(src->source) : nullptr) {
        w = asset->video.width; h = asset->video.height;
    }
    if (!w || !h) return none;
    DepthMapRequest r = request;
    r.host = src;
    r.localTime = src->local_time(planTime_);
    r.stateSalt = 0xAD7A000000000000ull ^ reinterpret_cast<uintptr_t>(&adj);
    DepthMapResult result = depth_map(r);
    if (!result.texture.valid()) return result;
    // px do quadro → px da fonte: a inversa da matriz 2D dela (com os pais).
    const Mat4 m = world_2d_frac(comp, *src, static_cast<f64>(planTime_.value));
    const f32 a = m.col[0].x, b = m.col[0].y, c = m.col[1].x, d = m.col[1].y;
    const f32 det = a * d - b * c;
    if (!std::isfinite(det) || std::fabs(det) < 1e-9f) return none;
    const f32 inv = 1.0f / det;
    const f32 tx = m.col[3].x, ty = m.col[3].y;
    result.mapX = Vec4{d * inv, -c * inv, (c * ty - d * tx) * inv, static_cast<f32>(w)};
    result.mapY = Vec4{-b * inv, a * inv, (b * tx - a * ty) * inv, static_cast<f32>(h)};
    return result;
}

TextureHandle Renderer::audio_spectrum(const AudioSpectrumRequest& request) noexcept {
    if (!backend_ || !planComp_ || !planProject_ || !planMedia_) return TextureHandle{};
    VideoSourceFactory* factory = planMedia_->factory();
    if (!factory) return TextureHandle{};
    const Composition& comp = *planComp_;
    const Project& project = *planProject_;
    const FrameIndex t = planTime_;

    // 1. Qual camada toca: a própria, a mais próxima abaixo, ou a primeira.
    const OrderedIds<LayerId>& order = comp.order();
    const Layer* source = nullptr;
    i32 hostIndex = -1;
    for (u32 i = 0; i < order.size() && hostIndex < 0; ++i) {
        if (comp.layer(order.at(i)) == request.host) hostIndex = static_cast<i32>(i);
    }
    if (request.source != AudioSpectrumSource::FirstWithAudio && request.host
        && layer_sounds_at(*request.host, project, t)) {
        source = request.host;
    } else if (request.source == AudioSpectrumSource::ThisLayer) {
        return TextureHandle{};
    }
    if (!source && request.source == AudioSpectrumSource::Automatic && hostIndex > 0) {
        for (i32 i = hostIndex - 1; i >= 0 && !source; --i) {
            const Layer* l = comp.layer(order.at(static_cast<u32>(i)));
            if (l && layer_sounds_at(*l, project, t)) source = l;
        }
    }
    if (!source) {
        for (u32 i = 0; i < order.size() && !source; ++i) {
            const Layer* l = comp.layer(order.at(i));
            if (l && layer_sounds_at(*l, project, t)) source = l;
        }
    }
    if (!source) return TextureHandle{};
    const Asset* asset = project.asset(source->source);
    if (!asset) return TextureHandle{};

    // 2. A amostra da fonte no instante — a MESMA conta do vídeo e do mixer.
    const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
    const f64 srcFrame = source->source_frame_f(static_cast<f64>(t.value));
    if (!std::isfinite(srcFrame)) return TextureHandle{};
    const i64 center = static_cast<i64>(std::llround(srcFrame * audio::kMixRate / fps));
    const i64 first = center - static_cast<i64>(audio::kSpectrumWindow / 2);
    const i64 length = asset->audio.sampleCount.value > 0 && asset->audio.sampleRate > 0
                     ? asset->audio.sampleCount.value * static_cast<i64>(audio::kMixRate) / asset->audio.sampleRate
                     : audio::frame_to_sample(asset->duration.value, asset->timebaseFps > 0.0 ? asset->timebaseFps : fps);
    if (length <= 0 || first + static_cast<i64>(audio::kSpectrumWindow) <= 0 || first >= length) return TextureHandle{};

    const u32 bands = std::clamp(request.bands, 1u, kAudioSpectrumMaxBands);
    const f32 gain = std::isfinite(request.gain) && request.gain > 0.0f ? request.gain : 1.0f;
    u32 gainBits = 0;
    std::memcpy(&gainBits, &gain, sizeof(gainBits));
    const u64 assetKey = source->source.pack();
    u64 key = assetKey * 0x9E3779B97F4A7C15ull;
    key ^= static_cast<u64>(first) * 0xBF58476D1CE4E5B9ull + 0x94D049BB133111EBull;
    key ^= (static_cast<u64>(bands) << 32) ^ gainBits;
    key ^= key >> 29;
    if (auto it = spectra_.find(key); it != spectra_.end()) {
        it->second.lastFrame = frameNumber_;
        return it->second.texture;
    }

    // 3. O trecho, decodificado agora (blocos ficam para os quadros vizinhos).
    if (!spectrumBlocks_ || spectrumFactory_ != factory) {
        spectrumBlocks_ = std::make_unique<audio::AudioBlockCache>(factory, 8ull << 20, false);
        spectrumFactory_ = factory;
    }
    if (!spectrumBlocks_->knows(assetKey)) {
        spectrumBlocks_->register_asset(assetKey, audio::AudioAssetRef{asset->sourcePath, length});
    }
    f32 mono[audio::kSpectrumWindow];
    std::shared_ptr<const audio::AudioBlock> block;
    i64 blockIndex = -1;
    for (u32 i = 0; i < audio::kSpectrumWindow; ++i) {
        const i64 s = first + static_cast<i64>(i);
        mono[i] = 0.0f;
        if (s < 0 || s >= length) continue;
        const i64 b = s / static_cast<i64>(audio::kBlockFrames);
        if (b != blockIndex) {
            blockIndex = b;
            block = spectrumBlocks_->fetch(assetKey, b);
        }
        if (!block) continue;
        const usize k = static_cast<usize>(s - b * static_cast<i64>(audio::kBlockFrames)) * audio::kMixChannels;
        if (k + 1 < block->pcm.size()) mono[i] = 0.5f * (block->pcm[k] + block->pcm[k + 1]);
    }

    // 4. A análise → textura `bands`×1 (R = faixa, G = nível).
    f32 magnitudes[kAudioSpectrumMaxBands];
    f32 level = 0.0f;
    audio::analyze_spectrum(mono, audio::kSpectrumWindow, bands, gain, magnitudes, &level);

    TextureDesc d;
    d.width = bands;
    d.height = 1;
    d.format = SurfaceFormat::RGBA16F;
    d.sampled = true;
    d.transferDst = true;
    d.debugName = "espectro-audio";
    auto tex = backend_->create_texture(d);
    if (!tex.ok()) return TextureHandle{};
    PendingUpload up;
    up.texture = *tex;
    up.bytesPerRow = bands * 8;
    up.data.resize(static_cast<usize>(bands) * 8);
    u16* px = reinterpret_cast<u16*>(up.data.data());
    for (u32 i = 0; i < bands; ++i) {
        px[i * 4 + 0] = to_half(magnitudes[i]);
        px[i * 4 + 1] = to_half(level);
        px[i * 4 + 2] = to_half(0.0f);
        px[i * 4 + 3] = to_half(1.0f);
    }
    uploads_.push_back(std::move(up));
    spectra_[key] = LutTexture{*tex, frameNumber_};
    return *tex;
}

// =============================================================================
// Fase 1 — prepare (sob o lock do modelo)
// =============================================================================
namespace {

/// Aprimorar: a chave dos vizinhos no cache das faixas (camada ^ sal), separada
/// da do TimeSlice da mesma camada (cada um solta as suas entradas).
constexpr u64 kNeighborSliceSalt = 0xE7A4'0000'0000'0000ull;

/// Parâmetro de efeito no instante LOCAL da camada (trilha, se houver).
f32 flow_fx_param(const Layer& l, const EffectInstance& e, u32 i, f64 local, f32 fallback, bool stepped) noexcept {
    f32 v = i < e.params.size() ? e.params[i].constant.as_float() : fallback;
    if (const Track* t = l.tracks.find(TrackProperty::EffectParam, e.id, param_track_key(i, 0)))
        v = stepped ? t->value_or(FrameIndex{static_cast<i64>(std::floor(local))}, v) : t->value_or_f(local, v);
    return std::isfinite(v) ? v : fallback;
}

/// Flow Retime: o instante da timeline cuja FONTE toca em `timelineTime`. A
/// velocidade (%) anda dentro da camada desde a entrada: ∫ velocidade dτ.
f64 flow_retime_timeline(const Layer& l, const EffectInstance& e, f64 timelineTime) noexcept {
    const f64 elapsed = std::max(0.0, timelineTime - static_cast<f64>(l.start.value));
    const f64 l0 = static_cast<f64>(l.offset.value);
    auto speed = [&](f64 local) {
        return std::clamp(static_cast<f64>(flow_fx_param(l, e, 0, local, 50.0f, false)), 1.0, 1000.0) / 100.0;
    };
    const Track* t = l.tracks.find(TrackProperty::EffectParam, e.id, param_track_key(0, 0));
    if (!t || t->keys.empty()) return static_cast<f64>(l.start.value) + elapsed * speed(l0);
    // Velocidade animada: ponto médio quadro a quadro (exato no segurado e no
    // linear; o easing erra muito abaixo de um quadro). Teto de 2^20 passos.
    const i64 n = std::min<i64>(static_cast<i64>(std::floor(elapsed)), i64{1} << 20);
    f64 acc = 0.0;
    for (i64 k = 0; k < n; ++k) acc += speed(l0 + static_cast<f64>(k) + 0.5);
    const f64 frac = elapsed - static_cast<f64>(n);
    if (frac > 0.0) acc += frac * speed(l0 + static_cast<f64>(n) + 0.5 * frac);
    return static_cast<f64>(l.start.value) + acc;
}

/// Os efeitos de tempo por optical flow da camada de vídeo no instante.
struct VideoFlowFx {
    const EffectInstance* retime = nullptr;
    f64 srcTimeline = 0.0;   ///< o instante da timeline que a fonte mostra
    f64 rate = 1.0;          ///< velocidade do retime agora (prefetch e direção)
    u8 frameBlend = 0;       ///< 0 repete, 1 mistura, 2 movimento
    f32 flowBase = 0.0f;     ///< base do optical flow (0 = padrão)
    f32 vectorAmount = 0.0f; ///< desfoque vetorial (0 = não)
    f64 shutter = 0.5;       ///< obturador em fração de quadro
    /// Flow Retime "Substituir quadros duplicados" (0.3.0) e a sensibilidade (0..100).
    bool replaceDuplicates = false;
    f32 duplicateSensitivity = 10.0f;
};

VideoFlowFx video_flow_fx(const Layer& l, f64 timelineTime, f64 compShutter) noexcept {
    VideoFlowFx fx;
    fx.srcTimeline = timelineTime;
    fx.frameBlend = l.frameBlend;
    fx.vectorAmount = l.vectorBlur;
    fx.shutter = compShutter;
    const EffectTypeId retimeType = effect_type_id(effect_keys::kFlowRetime);
    const EffectTypeId blurType = effect_type_id(effect_keys::kFlowMotionBlur);
    static constexpr f32 kBase[3] = {256.0f, 384.0f, 576.0f};
    const f64 local = timelineTime - static_cast<f64>(l.start.value) + static_cast<f64>(l.offset.value);
    for (const EffectInstance& e : l.effects) {
        if (!e.enabled) continue;
        if (e.type == retimeType && !fx.retime) {
            fx.retime = &e;
            fx.srcTimeline = flow_retime_timeline(l, e, timelineTime);
            fx.rate = std::clamp(static_cast<f64>(flow_fx_param(l, e, 0, local, 50.0f, false)), 1.0, 1000.0) / 100.0;
            fx.frameBlend = static_cast<u8>(std::clamp(static_cast<i32>(std::lround(flow_fx_param(l, e, 1, local, 2.0f, true))), 0, 2));
            fx.flowBase = kBase[std::clamp(static_cast<i32>(std::lround(flow_fx_param(l, e, 2, local, 1.0f, true))), 0, 2)];
            const f32 blur = std::clamp(flow_fx_param(l, e, 3, local, 0.0f, false), 0.0f, 400.0f) / 100.0f;
            if (blur > 0.0f) fx.vectorAmount = std::max(fx.vectorAmount, blur);
            fx.replaceDuplicates = flow_fx_param(l, e, 4, local, 0.0f, true) >= 0.5f;
            fx.duplicateSensitivity = std::clamp(flow_fx_param(l, e, 5, local, 10.0f, true), 0.0f, 100.0f);
        } else if (e.type == blurType) {
            const f32 amount = std::clamp(flow_fx_param(l, e, 0, local, 100.0f, false), 0.0f, 400.0f) / 100.0f;
            if (amount <= 0.0f) continue;
            fx.vectorAmount = amount;
            fx.shutter = std::clamp(static_cast<f64>(flow_fx_param(l, e, 1, local, 180.0f, false)), 0.0, 720.0) / 360.0;
            if (fx.flowBase <= 0.0f)
                fx.flowBase = kBase[std::clamp(static_cast<i32>(std::lround(flow_fx_param(l, e, 2, local, 1.0f, true))), 0, 2)];
        }
    }
    return fx;
}

/// O corte dos contornos da camada vetorial no instante (Progresso do
/// desenho): o primeiro efeito ligado que age na geometria
/// (`Effect::edits_vector_geometry`) manda. A regra mora no efeito e em
/// vector::build_mesh; aqui só se avaliam os valores dele, como no plano.
vector::DrawProgress vector_draw_progress(const Layer& l, const EffectRegistry* registry, f64 local) noexcept {
    vector::DrawProgress none;
    if (!registry) return none;
    std::vector<ParamValue> values;
    for (u32 i = 0; i < l.effects.size(); ++i) {
        const EffectInstance& inst = l.effects[i];
        if (!inst.enabled) continue;
        const Effect* fx = registry->find(inst.type);
        if (!fx || !fx->edits_vector_geometry()) continue;
        const ParameterRegistry* params = registry->params(inst.type);
        if (!params) continue;
        values.clear();
        for (u32 p = 0; p < params->count(); ++p) values.push_back(evaluate_param_f(l.tracks, inst, p, params->at(p), local));
        EffectEval e;
        e.effect = fx;
        e.instance = &inst;
        e.values = values.data();
        e.count = params->count();
        e.effectIndex = i;
        e.localTime = FrameIndex{static_cast<i64>(std::floor(local))};
        e.fractionalTime = local;
        e.layer = &l;
        vector::DrawProgress out;
        if (fx->vector_progress(e, out)) return out;
    }
    return none;
}

// =============================================================================
// CAMADA DE AJUSTE COM EFEITO DE TEMPO (0.1.8 BETA 2). Numa camada comum os
// efeitos de tempo pedem a FONTE noutro instante ao decoder; numa camada de
// ajuste a fonte é a composição abaixo, e o passado dela só existe
// renderizando-a de novo naquele instante. Aqui só se leem os parâmetros (a
// mesma conta do caminho da camada comum); o prepare pede os fundos.
// =============================================================================
struct AdjustTimeTypes { EffectTypeId echo, rgb, motionDetect, datamosh, wide = 0; };

struct AdjustTimeAsk {
    f64 shown = 0.0;          ///< instante da composição mostrado no lugar do atual
    bool replaced = false;
    u8 frameBlend = 0;        ///< 0 repete, 1 mistura, 2 movimento
    f32 flowBase = 0.0f;
    f32 vector = 0.0f;        ///< Flow Motion Blur (quantidade)
    f64 travel = 0.0;         ///< quadros do fundo que o obturador cobre
    bool rgb = false;
    f64 rgbAt[3]{};
    i64 historyDelay = 0;
    struct Echo { f64 at; f32 weight; Vec3 mask; };
    std::vector<Echo> echo;
    bool bare = false;        ///< efeito que lê o alfa: a pilha sem a cor de fundo
    std::vector<i32> neighbors;   ///< Aprimorar: a composição abaixo em t ± k
    [[nodiscard]] bool any() const noexcept {
        return replaced || rgb || historyDelay > 0 || !echo.empty() || bare || vector > 0.0f || frameBlend > 0
            || !neighbors.empty();
    }
};

/// Aprimorar e quem mais pedir vizinhos no tempo (Effect::temporal_neighbors):
/// os deslocamentos do PRIMEIRO efeito ligado que pede, nos valores do quadro.
/// O mesmo para a camada comum e para a de ajuste.
void temporal_neighbor_offsets(const Layer& l, const EffectRegistry& registry, FrameIndex local, f64 fps,
                               std::vector<i32>& out) {
    out.clear();
    for (const EffectInstance& inst : l.effects) {
        if (!inst.enabled) continue;
        const Effect* fx = registry.find(inst.type);
        if (!fx || !fx->wants_neighbors()) continue;
        const ParameterRegistry* specs = registry.params(inst.type);
        if (!specs) continue;
        std::vector<ParamValue> values;
        values.reserve(specs->count());
        for (u32 p = 0; p < specs->count(); ++p) values.push_back(evaluate_param(l.tracks, inst, p, specs->at(p), local));
        EffectEval evaluation;
        evaluation.effect = fx; evaluation.instance = &inst; evaluation.layer = &l;
        evaluation.values = values.data(); evaluation.count = specs->count();
        evaluation.localTime = local; evaluation.framesPerSecond = fps;
        i32 offsets[kMaxTemporalNeighbors]{};
        const u32 n = std::min<u32>(fx->temporal_neighbors(evaluation, offsets), kMaxTemporalNeighbors);
        for (u32 k = 0; k < n; ++k)
            if (offsets[k] != 0 && std::abs(offsets[k]) <= 3) out.push_back(offsets[k]);
        if (!out.empty()) return;
    }
}

/// Efeitos que leem o ALFA da camada (desenham fora dela ou no transparente):
/// numa camada de ajuste de tudo abaixo eles veem a pilha SEM a cor de fundo
/// (como no After Effects, onde o fundo da composição não é pixel de camada).
bool adjust_reads_alpha(EffectTypeId t) noexcept {
    static const EffectTypeId kTypes[] = {
        effect_type_id(effect_keys::kDropShadow), effect_type_id(effect_keys::kBorder),
        effect_type_id(effect_keys::kShadowStudio3), effect_type_id("aurea.key.solid_matte"),
        effect_type_id("aurea.stylize.stroke"), effect_type_id("aurea.stylize.stroke_outline"),
        effect_type_id("aurea.stylize.bevel_alpha"), effect_type_id("aurea.key.matte_choker"),
        effect_type_id("aurea.key.matte_refine"),
    };
    return std::find(std::begin(kTypes), std::end(kTypes), t) != std::end(kTypes);
}

AdjustTimeAsk adjust_time_ask(const Layer& l, const EffectRegistry& registry, const EffectPlan& plan, FrameIndex time,
                              FrameIndex layerTime, f64 fps, f64 compShutter, bool allBelow, AdjustTimeTypes types) {
    AdjustTimeAsk ask;
    const FrameIndex local = l.local_time(layerTime);
    // Posterizar tempo / Low FPS: o prepare já travou `layerTime`.
    ask.shown = static_cast<f64>(layerTime.value);
    ask.replaced = layerTime != time;
    // Remapear tempo: a curva da camada diz que quadro da "fonte" (a
    // composição abaixo, com o quadro 0 na entrada da camada) toca agora.
    const EffectTypeId remapType = effect_type_id(effect_keys::kTimeRemap);
    const bool remap = l.timeRemapEnabled && !l.timeRemap.keys.empty()
        && std::any_of(l.effects.begin(), l.effects.end(), [&](const EffectInstance& e) { return e.enabled && e.type == remapType; });
    const EffectTypeId retimeType = effect_type_id(effect_keys::kFlowRetime);
    const EffectTypeId flowBlurType = effect_type_id(effect_keys::kFlowMotionBlur);
    const bool flowFx = std::any_of(l.effects.begin(), l.effects.end(), [&](const EffectInstance& e) {
        return e.enabled && (e.type == retimeType || e.type == flowBlurType);
    });
    const VideoFlowFx flow = flowFx ? video_flow_fx(l, ask.shown, compShutter) : VideoFlowFx{};
    auto shown_at = [&](f64 timeline) {
        f64 t = flow.retime ? flow_retime_timeline(l, *flow.retime, timeline) : timeline;
        if (remap) t = static_cast<f64>(l.start.value - l.offset.value) + l.source_frame_f(t);
        return t;
    };
    if (remap || flow.retime) {
        ask.shown = shown_at(static_cast<f64>(layerTime.value));
        ask.replaced = true;
    }
    if (flowFx) {
        ask.frameBlend = flow.retime ? flow.frameBlend : 0;
        ask.flowBase = flow.flowBase;
        if (flow.vectorAmount > 0.0f) {
            const f64 h = flow.shutter * 0.5, t0 = static_cast<f64>(layerTime.value);
            const f64 c = shown_at(t0);
            ask.travel = std::fabs(c - shown_at(t0 - h)) + std::fabs(shown_at(t0 + h) - c);
            ask.vector = flow.vectorAmount;
        }
    }
    // RGB no tempo: cada canal de um instante (o mesmo do caminho comum).
    if (types.rgb) {
        if (const ParameterRegistry* rp = registry.params(types.rgb); rp && rp->count() >= 6) {
            for (const EffectInstance& inst : l.effects) {
                if (!inst.enabled || inst.type != types.rgb) continue;
                const bool seconds = evaluate_param(l.tracks, inst, 3, rp->at(3), local).as_enum() == 1;
                const f32 amount = std::clamp(evaluate_param(l.tracks, inst, 4, rp->at(4), local).v[0] / 100.0f, 0.0f, 1.0f);
                const f64 unit = seconds ? fps : 1.0;
                if (amount >= 0.01f) {
                    ask.rgb = true;
                    for (u32 c = 0; c < 3; ++c)
                        ask.rgbAt[c] = static_cast<f64>(time.value)
                            + static_cast<f64>(evaluate_param(l.tracks, inst, c, rp->at(c), local).v[0]) * unit * amount;
                }
                break;
            }
        }
    }
    // O passado: Detectar movimento, Datamosh e quem pede `history_delay_frames`.
    const ParameterRegistry* mp = types.motionDetect ? registry.params(types.motionDetect) : nullptr;
    const ParameterRegistry* dp = types.datamosh ? registry.params(types.datamosh) : nullptr;
    const u32 holdIndex = dp ? dp->find("hold_frames") : kInvalidIndex;
    for (const EffectInstance& inst : l.effects) {
        if (!inst.enabled) continue;
        if (types.datamosh && inst.type == types.datamosh && dp && holdIndex < dp->count()) {
            const f32 q = evaluate_param(l.tracks, inst, holdIndex, dp->at(holdIndex), local).v[0];
            const i64 hold = std::isfinite(q) ? std::clamp<i64>(std::llround(q), 1, kDatamoshMaxHold) : 12;
            ask.historyDelay = ((local.value % hold) + hold) % hold + 1;
            break;
        }
        if (inst.type == types.motionDetect && mp && mp->count() >= 1) {
            const f32 d = evaluate_param(l.tracks, inst, 0, mp->at(0), local).v[0];
            ask.historyDelay = std::isfinite(d) ? std::clamp<i64>(std::llround(d), 1, kMotionDetectMaxDelay) : 1;
            break;
        }
        const Effect* fx = registry.find(inst.type);
        const ParameterRegistry* specs = fx && fx->wants_history() ? registry.params(inst.type) : nullptr;
        if (!specs) continue;
        std::vector<ParamValue> values;
        values.reserve(specs->count());
        for (u32 p = 0; p < specs->count(); ++p) values.push_back(evaluate_param(l.tracks, inst, p, specs->at(p), local));
        EffectEval evaluation;
        evaluation.effect = fx; evaluation.instance = &inst; evaluation.layer = &l;
        evaluation.values = values.data(); evaluation.count = specs->count();
        evaluation.localTime = local; evaluation.framesPerSecond = fps;
        const f64 delay = fx->history_delay_frames(evaluation);
        if (std::isfinite(delay) && delay >= 1.0) {
            ask.historyDelay = std::clamp<i64>(static_cast<i64>(std::floor(delay)), 1, 1200);
            break;
        }
    }
    // Eco e rastro: cópias atrasadas que vão sumindo (somadas, como numa
    // pré-composição) e o atraso RGB. Com o RGB no tempo ligado, ele manda.
    if (types.echo && !ask.rgb) {
        if (const ParameterRegistry* ep = registry.params(types.echo); ep && ep->count() >= 4) {
            for (const EffectInstance& inst : l.effects) {
                if (!inst.enabled || inst.type != types.echo) continue;
                const u32 count = static_cast<u32>(std::clamp(evaluate_param(l.tracks, inst, 0, ep->at(0), local).v[0], 0.0f, 16.0f) + 0.5f);
                const f32 delay = evaluate_param(l.tracks, inst, 1, ep->at(1), local).v[0];
                const f32 decay = evaluate_param(l.tracks, inst, 2, ep->at(2), local).v[0];
                const f32 rgbDelay = evaluate_param(l.tracks, inst, 3, ep->at(3), local).v[0];
                if (count == 0 && !(rgbDelay > 0.0f)) break;
                const f64 t0 = static_cast<f64>(time.value);
                if (rgbDelay > 0.0f) {
                    const f64 d = std::clamp(static_cast<f64>(rgbDelay), 0.0, 60.0);
                    ask.echo.push_back({t0, 1.0f, Vec3{1, 0, 0}});
                    ask.echo.push_back({t0 - d, 1.0f, Vec3{0, 1, 0}});
                    ask.echo.push_back({t0 - 2.0 * d, 1.0f, Vec3{0, 0, 1}});
                } else {
                    ask.echo.push_back({t0, 1.0f, Vec3{0, 0, 0}});
                }
                const f64 d = std::clamp(static_cast<f64>(delay), 0.25, 120.0);
                f32 w = 1.0f;
                for (u32 k = 1; k <= std::min<u32>(count, 16u); ++k) {
                    w *= std::clamp(decay, 0.0f, 1.0f);
                    if (w < 0.01f) break;
                    ask.echo.push_back({t0 - static_cast<f64>(k) * d, w, Vec3{0, 0, 0}});
                }
                break;
            }
        }
    }
    // TEMPO AMPLO (0.3.0): o uso do AE — numa camada de ajuste, a média da
    // composição abaixo de t − trás a t + frente (pesos iguais). Cada quadro é
    // um fundo re-renderizado; com mais quadros que o orçamento do quadro
    // (24 fundos), a janela é amostrada em passo uniforme, pesos iguais. Aqui
    // a amostra já é o quadro inteiro da composição abaixo em cada instante;
    // o "Desfoque nativo" (subamostras) vale para a camada que se move.
    if (types.wide && ask.echo.empty() && !ask.rgb) {
        if (const ParameterRegistry* wp = registry.params(types.wide); wp && wp->count() >= 4) {
            for (const EffectInstance& inst : l.effects) {
                if (!inst.enabled || inst.type != types.wide) continue;
                const i32 fwd = std::clamp(evaluate_param(l.tracks, inst, 0, wp->at(0), local).as_int(), 0, 30);
                const i32 back = std::clamp(evaluate_param(l.tracks, inst, 1, wp->at(1), local).as_int(), 0, 30);
                if (fwd + back == 0) break;
                const i32 stride = (fwd + back + 1 + 22) / 23;   // ≤ 23 fundos extras + o de agora
                std::vector<i32> offsets;   // o de agora e as pontas sempre entram
                for (i32 k = -back; k <= fwd; ++k)
                    if (k % stride == 0 || k == -back || k == fwd) offsets.push_back(k);
                const f32 w = 1.0f / static_cast<f32>(offsets.size());
                const f64 t0 = static_cast<f64>(time.value);
                for (const i32 k : offsets) ask.echo.push_back({t0 + static_cast<f64>(k), w, Vec3{0, 0, 0}});
                break;
            }
        }
    }
    if (allBelow)
        for (const EffectEval& ev : plan.evals) ask.bare |= ev.effect && adjust_reads_alpha(ev.effect->type_id());
    // Aprimorar: os vizinhos são quadros da composição abaixo (t ± k).
    temporal_neighbor_offsets(l, registry, local, fps, ask.neighbors);
    return ask;
}

} // namespace

void Renderer::prepare(const Composition& comp, const Project& project, FrameIndex time,
                       MediaManager* media, const ImagePixels* (*imageLookup)(void*, AssetId),
                       void* imageCtx, const RenderSettings& settings, u64 frameNumber,
                       i32 playDirection, DecodeMode decodeMode, f32 speed, FrameSnapshot& out) {
    if (previewGraph_.active) return;
    frameNumber_ = frameNumber;
    // O espectro de áudio (EffectResources) precisa saber DE QUE composição,
    // projeto e instante o planejamento é — dentro de uma pré-composição, os
    // dela. Guarda o de fora e devolve ao sair.
    struct PlanScope {
        Renderer& r;
        const Composition* comp;
        const Project* project;
        MediaManager* media;
        FrameIndex time;
        PlanScope(Renderer& self, const Composition& c, const Project& p, MediaManager* m, FrameIndex t)
            : r(self), comp(self.planComp_), project(self.planProject_), media(self.planMedia_), time(self.planTime_) {
            r.planComp_ = &c; r.planProject_ = &p; r.planMedia_ = m; r.planTime_ = t;
        }
        ~PlanScope() { r.planComp_ = comp; r.planProject_ = project; r.planMedia_ = media; r.planTime_ = time; }
    } planScope{*this, comp, project, media, time};
    // O mapa de profundidade (RendererDepth.cpp) lê os pixels das imagens e
    // precisa saber se o quadro é de export; numa pré-composição, o mesmo.
    struct DepthScope {
        Renderer& r;
        const ImagePixels* (*lookup)(void*, AssetId);
        void* ctx;
        bool final;
        bool defer;
        DepthScope(Renderer& self, const ImagePixels* (*l)(void*, AssetId), void* c, bool f, bool d)
            : r(self), lookup(self.planImageLookup_), ctx(self.planImageCtx_), final(self.planFinal_), defer(self.planDeferLocalAi_) {
            r.planImageLookup_ = l; r.planImageCtx_ = c; r.planFinal_ = f; r.planDeferLocalAi_ = d;
        }
        ~DepthScope() { r.planImageLookup_ = lookup; r.planImageCtx_ = ctx; r.planFinal_ = final; r.planDeferLocalAi_ = defer; }
    } depthScope{*this, imageLookup, imageCtx, settings.finalQuality, settings.deferLocalAi};
    planPlayback_ = decodeMode == DecodeMode::Playback;
    // FUNDO DE UMA CAMADA DE AJUSTE noutro instante (efeitos de tempo): só as
    // camadas abaixo dela (no escopo dela). Vale só para ESTA composição: as
    // pré-composições lá dentro se desenham inteiras.
    const u32 stackLimit = backdropLimit_;
    const u32 stackFrom = backdropFrom_;
    std::vector<LayerId> stackOnly;
    stackOnly.swap(backdropOnly_);
    const bool stackBare = backdropBare_;
    backdropLimit_ = ~0u;
    backdropFrom_ = 0;
    backdropBare_ = false;
    if (prepareDepth_ == 0) {
        // Um eco de 16 cópias sobre outro eco de 16 seriam 256 composições:
        // o teto vale por quadro inteiro, igual na prévia e no export.
        backdropBudget_ = 24;
        CompositionId rootComposition{};
        project.timeline().for_each_composition([&](CompositionId id, const Composition& candidate) {
            if (&candidate == &comp) rootComposition = id;
        });
        renderInstancePath_.assign(1, rootComposition.pack());
        localAiPending_ = 0;
        quality_ = effective_quality(settings);
        // Child snapshots and all of their uploads coexist until the same
        // frame fence. A per-composition budget would multiply with nesting.
        exposureBytesRemaining_ = 32ull * 1024 * 1024;
    }
    // Fora do playback contínuo (parado, scrub, export): o que o projeto usa e
    // ainda não foi compilado entra na fila; render() compila antes do grafo.
    // No playback não varre — nada muda no modelo sem pausar.
    if (prepareDepth_ == 0 && decodeMode != DecodeMode::Playback) scan_for_warmup(project);
    // Expressões: a timeline do quadro (camada dona, outras camadas) e o memo
    // por (propriedade, quadro). O quadro é preparado sob o lock do modelo.
    const expr::Scope exprScope(project.timeline());
    out.layers.clear();
    out.nested.clear();
    out.sceneExposureBatches.clear();
    out.glyphs.clear();
    out.maskData.clear();
    out.vec.clear();
    out.particleData.clear();
    // Malhas vetoriais de camadas que saíram de cena há tempo: fora do cache.
    if (prepareDepth_ == 0 && vectorCache_.size() > 32) {
        for (auto it = vectorCache_.begin(); it != vectorCache_.end();) {
            if (it->second.lastFrame + 300 < frameNumber) it = vectorCache_.erase(it);
            else ++it;
        }
    }
    out.target = FGTexture{};
    out.compWidth = comp.width();
    out.compHeight = comp.height();
    // O alvo é pré-multiplicado: fundo transparente limpa em (0,0,0,0). Limpar
    // com a cor guardada e alfa 0 somava essa cor às bordas semitransparentes
    // (fundo branco "sem fundo" clareava tudo no preview e no PNG).
    const Color bg = comp.background();
    out.background = comp.transparent_background() || stackBare
        ? Vec4{0.0f, 0.0f, 0.0f, 0.0f}
        : Vec4{srgb_to_linear(bg.r), srgb_to_linear(bg.g), srgb_to_linear(bg.b), 1.0f};
    out.time = time;
    out.playbackPtsUs = -1;
    out.playbackStream = {};
    out.playbackDecode = {};
    out.videoLayers = 0;
    out.staleVideoFrames = 0;
    out.missingVideoFrames = 0;
    out.videoSourceWaits.clear();
    out.culledLayers = 0;

    const f32 previewFactor = static_cast<f32>(settings.previewNumerator)
                            / static_cast<f32>(std::max(1u, settings.previewDenominator));
    // Câmera da cena para camadas 2D que vivem no espaço 3D.
    const scene3d::SceneCamera cam3d = settings.sceneEditor.enabled && !settings.finalQuality
        ? scene_editor_camera(out.compWidth, out.compHeight, settings.sceneEditor) : camera_for(comp, time, out.compWidth, out.compHeight);
    const Mat4 viewProj3d = cam3d.imageTransform * scene3d::reverse_z_perspective(
                                cam3d.fovY, static_cast<f32>(out.compWidth) / static_cast<f32>(std::max(1u, out.compHeight)),
                                cam3d.nearZ) * cam3d.view;
    const Mat4 compFromClip = comp_from_clip(static_cast<f32>(out.compWidth), static_cast<f32>(out.compHeight));
    const CameraPassThrough inheritedCamera = g_cameraPassThrough;
    const bool effectCameraActive = has_active_camera(comp, time)
        || (inheritedCamera.child == &comp && inheritedCamera.parent
            && has_active_camera(*inheritedCamera.parent, FrameIndex{static_cast<i64>(std::floor(
                inheritedCamera.parentTime0 + (time.value - inheritedCamera.childTime0) * inheritedCamera.rate))}));
    const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;

    // A ORDEM DO CORE É A ORDEM DE COMPOSIÇÃO: `order()` do fundo para a
    // frente. Nenhuma lista paralela, nenhum reordenamento por tipo.
    const OrderedIds<LayerId>& order = comp.order();
    const u32 n = order.size();
    u32 used = 0;
    out.scenes.clear();
    // The panorama is a bottom layer, even in a camera-only scene. Never
    // repeat it in subsequent 3D groups (which would hide lower 2D layers).
    if (stackFrom == 0 && stackOnly.empty()
        && comp.environment().background_at(time) && (comp.environment().hdri.valid() || comp.environment().studioPreset != 0)) {
        out.scenes.emplace_back();
        out.scenes.back().environment.showBackground = true;
        RenderLayer sky;
        sky.id = LayerId{0xFFFFFFFEu, nestSalt_ + 1};
        sky.source.kind = LayerSource::Kind::Scene3D; sky.source.sceneGroup = 0;
        sky.source.width = out.compWidth; sky.source.height = out.compHeight;
        sky.compFromLayer = Mat4::identity(); sky.texelScale = previewFactor;
        out.layers.push_back(std::move(sky));
        if (out.plans.empty()) out.plans.emplace_back();
        out.plans[0].clear(); ++used;
    }
    // Layers 3D CONSECUTIVAS na pilha formam um grupo (uma cena, uma
    // profundidade compartilhada: elas se ocluem). Qualquer layer 2D entre
    // elas fecha o grupo — a ordem da pilha continua sendo a lei.
    bool groupOpen = false;
    // Solo (só no preview): com alguma camada visível em solo, as outras que
    // desenham ficam de fora. Câmera e luz não entram na regra (a cena
    // continua enquadrada e iluminada, como no AE).
    bool anySolo = false;
    if (!settings.finalQuality) {
        for (u32 i = 0; i < n && !anySolo; ++i) {
            const Layer* l = comp.layer(order.at(i));
            anySolo = l && l->visible && l->solo && l->kind != LayerKind::Camera && l->kind != LayerKind::Light;
        }
    }
    // Dentro de uma pré-composição os ids se repetem (cada composição tem a
    // sua tabela): decoder, texto e caches precisam de uma chave própria.
    auto render_id = [this](LayerId id) {
        return nestSalt_ == 0 ? id : LayerId{0x40000000u | ((nestSalt_ & 0x3FFFu) << 16) | (id.index & 0xFFFFu), id.generation};
    };
    // Track matte: camadas usadas como matte por alguma outra. Elas não
    // desenham por conta própria (mesmo com o olho desligado, recortam).
    std::vector<u64> mattes;
    for (u32 i = 0; i < n; ++i) {
        const Layer* l = comp.layer(order.at(i));
        if (l && l->matteMode != MatteMode::None && l->matteSource.valid() && l->matteSource.pack() != order.at(i).pack()) {
            mattes.push_back(l->matteSource.pack());
        }
    }
    // Camadas lidas como IMAGEM por um efeito de outra (o mapa do Mapa de
    // deslocamento): entram no quadro mesmo com o olho desligado; aí só
    // servem de entrada (como a matte), não desenham por conta própria.
    std::vector<u64> inputLayers;
    if (effects_) {
        for (u32 i = 0; i < n; ++i) {
            const Layer* l = comp.layer(order.at(i));
            if (!l || !l->contains_time(time)) continue;
            for (const EffectInstance& inst : l->effects) {
                const Effect* fx = inst.enabled ? effects_->find(inst.type) : nullptr;
                for (u32 slot = 0; fx && slot < fx->input_layer_count(); ++slot) {
                    const i32 k = fx->input_layer_param_at(slot);
                    if (k < 0 || static_cast<u32>(k) >= inst.params.size()) continue;
                    const u64 ref = inst.params[static_cast<u32>(k)].constant.ref;
                    if (ref && ref != order.at(i).pack()) inputLayers.push_back(ref);
                }
            }
        }
    }
    // Escopo 1 de uma camada de ajuste: onde começa o último desenho da pilha
    // (um grupo 3D conta como um). ~0u = nada que ela possa ajustar.
    u32 lastDrawOrder = ~0u;
    // MÁSCARA DE RECORTE: a base de cada recortada NESTE instante. A pilha é a
    // das camadas que existem no tempo (fora do trecho dela a camada não conta):
    // o 2º pedaço de uma base dividida recorta as de cima no trecho dele, o 1º
    // no dele. Uma base escondida (olho, opacidade 0, solo) continua sendo a
    // base — e não desenha nada, então as recortadas somem junto (como no
    // Photoshop). Recortada sem base válida embaixo: não aparece. Contado no
    // próprio laço (nenhuma alocação por quadro).
    i32 clipBaseIndex = -1;
    bool clipBaseOk = false;
    for (u32 i = 0; i < n && i < stackLimit; ++i) {
        const LayerId id = order.at(i);
        const Layer* l = comp.layer(id);
        if (!l || !l->contains_time(time)) continue;
        u64 clipBaseOf = 0;          // id com sal da base; 0 = não recortada
        bool clipHidden = false;     // recortada sem base válida embaixo
        bool clipHasChild = false;   // a próxima camada no instante é recortada por esta
        if (l->clipToBelow && can_be_clip_child(*l)) {
            if (clipBaseIndex >= 0 && clipBaseOk) clipBaseOf = render_id(order.at(static_cast<u32>(clipBaseIndex))).pack();
            else clipHidden = true;
        } else {
            clipBaseIndex = static_cast<i32>(i);
            clipBaseOk = can_be_clip_base(*l);
            for (u32 j = i + 1; clipBaseOk && j < n; ++j) {
                const Layer* above = comp.layer(order.at(j));
                if (!above || !above->contains_time(time)) continue;
                clipHasChild = above->clipToBelow && can_be_clip_child(*above);
                break;
            }
        }
        if (nestSalt_ && id.index > 0xFFFFu) { mark_incomplete(__FILE__, __LINE__); continue; }
        const bool isMatte = std::find(mattes.begin(), mattes.end(), id.pack()) != mattes.end();
        const bool isInput = std::find(inputLayers.begin(), inputLayers.end(), id.pack()) != inputLayers.end();
        if (!l->visible && !isMatte && !isInput) continue;
        // Fundo de camada de ajuste de escopo 1/2: fora do escopo, só como
        // matte ou entrada de outra (não desenha).
        const bool outsideStack = i < stackFrom
            || (!stackOnly.empty() && std::find(stackOnly.begin(), stackOnly.end(), id) == stackOnly.end());
        if (outsideStack && !isMatte && !isInput) continue;
        // Guia: referência de trabalho no editor, nunca no arquivo final.
        if (l->guide && settings.finalQuality) continue;
        if (anySolo && !l->solo && !isMatte && !isInput) continue;
        const bool inputOnly = isInput && (!l->visible || (anySolo && !l->solo) || outsideStack);
        if (clipHidden && !isMatte && !isInput) continue;   // recortada sem base: nada aparece
        // POSTERIZAR TEMPO (Fase 7.3 §26): o tempo da camada é travado numa
        // taxa menor ANTES de tudo. Trava o TEMPO DA TIMELINE, não o "tempo
        // local": quem decide o quadro do vídeo é `source_frame(timelineTime)`,
        // então travar o tempo local deixaria o vídeo andando normalmente — e
        // o efeito não seria um efeito, seria um nome no painel.
        FrameIndex layerTime = time;
        if (effects_) {
            const EffectTypeId posterize = posterizeType_;
            const ParameterRegistry* pp = posterize ? effects_->params(posterize) : nullptr;
            if (pp && pp->count() >= 2) {
                for (const EffectInstance& inst : l->effects) {
                    if (!inst.enabled || inst.type != posterize) continue;
                    const f32 fps = evaluate_param(l->tracks, inst, 0, pp->at(0), l->local_time(time)).v[0];
                    const bool hold = evaluate_param(l->tracks, inst, 1, pp->at(1), l->local_time(time)).as_bool();
                    const f64 step = comp.fps() / std::max(fps, 0.01f);
                    if (step > 1.0) {
                        const f64 v = static_cast<f64>(time.value) / step;
                        layerTime = FrameIndex{static_cast<i64>((hold ? std::floor(v) : std::round(v)) * step)};
                    }
                    break;   // um Posterizar por camada: o primeiro ligado manda
                }
            }
            // LOW FPS: o mesmo travamento, com ritmo irregular — a fronteira de
            // cada quadro segurado anda um sorteio (até meio passo) para frente
            // ou para trás. O tremido é o próprio efeito (TemporalEffects.cpp).
            const ParameterRegistry* lp = lowFpsType_ ? effects_->params(lowFpsType_) : nullptr;
            if (layerTime == time && lp && lp->count() >= 5) {
                for (const EffectInstance& inst : l->effects) {
                    if (!inst.enabled || inst.type != lowFpsType_) continue;
                    const FrameIndex lt = l->local_time(time);
                    const f32 fps = evaluate_param(l->tracks, inst, 0, lp->at(0), lt).v[0];
                    const f32 irregular = std::clamp(evaluate_param(l->tracks, inst, 3, lp->at(3), lt).v[0] * .01f, 0.f, 1.f);
                    const f32 seed = evaluate_param(l->tracks, inst, 4, lp->at(4), lt).v[0];
                    const f64 step = comp.fps() / std::max(fps, 0.01f);
                    if (step > 1.0) {
                        const f64 v = static_cast<f64>(time.value) / step;
                        const f64 cell = std::floor(v);
                        const f64 h = std::sin((cell + 1.0) * 12.9898 + static_cast<f64>(seed) * 78.233) * 43758.5453;
                        const f64 shift = (h - std::floor(h) - 0.5) * irregular;
                        const f64 k = std::floor(v - shift);
                        layerTime = FrameIndex{std::min(time.value, static_cast<i64>(std::max(0.0, k) * step))};
                    }
                    break;
                }
            }
            // EDIÇÕES ALEATÓRIAS (0.2.7): a camada em trechos de N quadros,
            // remontados numa ordem embaralhada pela semente
            // (effects/RandomEdits.hpp). Depois do Posterizar/Low FPS: o
            // trecho escolhido ainda anda no ritmo travado.
            const ParameterRegistry* rp = randomEditsType_ ? effects_->params(randomEditsType_) : nullptr;
            if (rp && rp->count() >= 2) {
                for (const EffectInstance& inst : l->effects) {
                    if (!inst.enabled || inst.type != randomEditsType_) continue;
                    const FrameIndex lt = l->local_time(layerTime);
                    const i64 length = std::max<i64>(1, evaluate_param(l->tracks, inst, 0, rp->at(0), lt).as_int());
                    const f32 seed = evaluate_param(l->tracks, inst, 1, rp->at(1), lt).v[0];
                    const i64 duration = l->end.value - l->start.value;
                    const i64 elapsed = layerTime.value - l->start.value;
                    layerTime = FrameIndex{l->start.value + random_edits::frame(elapsed, duration, length, seed)};
                    break;
                }
            }
        }
        const FrameIndex local = l->local_time(layerTime);

        const LayerId rid = render_id(id);
        RenderLayer rl;
        rl.matteOnly = isMatte || inputOnly;
        rl.clipBase = clipBaseOf;
        rl.clipParent = clipHasChild;
        // No grupo de recorte a camada é um desenho 2D próprio (como a matte):
        // não vira plano da cena 3D nem divide a cena de outro modelo.
        const bool clipInvolved = rl.clipBase != 0 || rl.clipParent;
        if (l->matteMode != MatteMode::None && l->matteSource.valid() && l->matteSource.pack() != id.pack()) {
            rl.matteMode = l->matteMode;
            rl.matteId = render_id(l->matteSource).pack();
        }
        // Margem além da que a âncora do texto conta (animadores, fundo, sombra):
        // a fonte cresce em volta, o texto não anda.
        // (Vetor: a caixa do conteúdo, que pode começar em coordenada negativa.)
        Vec2 srcShift{0.0f, 0.0f};
        rl.id = rid;
        rl.blend = l->blendMode;
        rl.opacity = layer_opacity(*l, local, fps);
        rl.opacity *= grid::evaluate(comp,*l,static_cast<f64>(time.value)).opacity;
        if (rl.opacity <= 0.0f) continue;   // invisível: nenhum passe, nenhum decode

        if (l->kind == LayerKind::Light && effects_) {
            const auto type = effect_type_id(effect_keys::kSceneFlare);
            const bool flare = std::any_of(l->effects.begin(), l->effects.end(), [type](const EffectInstance& fx) {
                return fx.enabled && fx.type == type;
            });
            if (!flare) continue;
            const Vec4 clip = viewProj3d * world_3d(comp, *l, time) * Vec4{0, 0, 0, 1};
            // A source behind the camera or inside its near plane has no visible flare.
            if (!(clip.w > 1e-4f && clip.z >= 0 && clip.z <= clip.w)) continue;
            const Vec4 screen = compFromClip * clip;
            if (!std::isfinite(screen.x / screen.w) || !std::isfinite(screen.y / screen.w)) continue;
            rl.source.kind = LayerSource::Kind::Solid;
            rl.source.solid = {0, 0, 0, 0};
            rl.source.width = out.compWidth; rl.source.height = out.compHeight;
            rl.compFromLayer = Mat4::identity(); rl.texelScale = previewFactor;
            LayerPlacement placement;
            placement.compWidth = placement.layerWidth = out.compWidth;
            placement.compHeight = placement.layerHeight = out.compHeight;
            placement.sceneFlare = true;
            placement.flarePosition = {screen.x / screen.w, screen.y / screen.w};
            if (out.plans.size() <= used) out.plans.emplace_back();
            EffectGraph::plan(*l, *effects_, local, previewFactor, placement, this, out.plans[used], fps, &comp);
            if (out.plans[used].empty()) continue;
            groupOpen = false;
            out.layers.push_back(std::move(rl)); ++used;
            lastDrawOrder = i;
            continue;
        }

        if (l->adjustment) {
            // Camada de ajuste: o "conteúdo" é a composição acumulada abaixo
            // dela, no quadro inteiro (px da camada = px da composição). Os
            // efeitos são planejados como os de qualquer camada; a montagem
            // acontece na composição, quando o fundo já existe.
            rl.source.kind = LayerSource::Kind::Adjustment;
            rl.adjustScope = l->adjustmentScope;
            if (l->adjustmentScope == 2)
                for (LayerId t : l->adjustmentTargets) rl.adjustTargets.push_back(render_id(t).pack());
            rl.source.width = out.compWidth;
            rl.source.height = out.compHeight;
            rl.compFromLayer = Mat4::identity();
            rl.texelScale = previewFactor;
            // Modo de mistura da camada de ajuste: o resultado dos efeitos é
            // misturado ao fundo por ele (Tela, Clarear...) e depois pesado pela
            // opacidade. Normal = o de sempre (o resultado substitui o fundo).
            rl.blend = l->blendMode;
            // Máscaras da camada de ajuste: limitam ONDE os efeitos valem. Fora
            // delas o fundo fica intacto (antes eram ignoradas).
            if (!l->masks.empty()) {
                rl.maskFirst = static_cast<u32>(out.maskData.size());
                rl.maskCount = mask::build_block(*l, static_cast<f64>(local.value), Vec2{0.0f, 0.0f},
                                                 0.2f / std::max(rl.texelScale, 1e-3f), out.maskData, rl.maskStart, rl.maskKey);
            }
            if (out.plans.size() <= used) out.plans.emplace_back();
            LayerPlacement pl;
            pl.compFromLayer = Mat4::identity();
            pl.compWidth = out.compWidth;
            pl.compHeight = out.compHeight;
            pl.layerWidth = out.compWidth;
            pl.layerHeight = out.compHeight;
            EffectGraph::plan(*l, *effects_, local, rl.texelScale, pl, this, out.plans[used], fps);
            EffectPlan& adjustPlan = out.plans[used];
            // Só Transform/Agitar/Oscilar... (dobrados na matriz): a pilha fica
            // sem etapa, mas a geometria do fundo muda (antes a camada saía).
            if (adjustPlan.hasFold)
                for (auto& column : adjustPlan.foldMatrix.col) column.z = 0.0f;   // 2D: sem profundidade
            // TEMPO e ALFA: a entrada é a composição abaixo em OUTRO instante
            // ou sem a cor de fundo — renderizada de novo, só com as camadas
            // abaixo (no escopo). Determinístico: o mesmo quadro dá o mesmo
            // resultado no scrub, na reprodução e no export.
            const AdjustTimeAsk ask = adjust_time_ask(*l, *effects_, adjustPlan, time, layerTime, fps,
                static_cast<f64>(comp.motion_blur().shutterAngle) / 360.0, rl.adjustScope == 0,
                AdjustTimeTypes{echoType_, rgbTimeType_, motionDetectType_, datamoshType_, wideTimeType_});
            const bool canAdjust = !(rl.adjustScope == 1 && lastDrawOrder == ~0u);
            if (ask.any() && canAdjust && prepareDepth_ < 8) {
                const u32 targets = rl.adjustScope == 2 ? static_cast<u32>(l->adjustmentTargets.size()) : 1u;
                rl.adjustTime.resize(targets);
                for (u32 target = 0; target < targets; ++target) {
                    // (instante, sem fundo) -> índice em out.nested, por alvo.
                    std::vector<std::pair<std::pair<i64, bool>, u32>> made;
                    auto backdropAt = [&](f64 at, u32 purpose, bool bare) -> u32 {
                        const i64 frame = std::max<i64>(0, static_cast<i64>(std::floor(at + 1e-6)));
                        if (frame == time.value && !bare) return ~0u;   // o fundo de agora
                        for (const auto& [key, index] : made) if (key.first == frame && key.second == bare) return index;
                        if (backdropBudget_ == 0) return ~0u;
                        --backdropBudget_;
                        auto snap = std::make_unique<FrameSnapshot>();
                        const u32 savedSalt = nestSalt_;
                        const usize savedPathSize = renderInstancePath_.size();
                        renderInstancePath_.push_back(id.pack());
                        renderInstancePath_.push_back(0x7D000000ull + (static_cast<u64>(target) << 8) + purpose);
                        auto ns = nestedNamespaces_.find(renderInstancePath_);
                        if (ns == nestedNamespaces_.end()) {
                            if (nestedNamespaces_.size() >= 0x3FFFu) {
                                renderInstancePath_.resize(savedPathSize);
                                mark_incomplete(__FILE__, __LINE__);
                                return ~0u;
                            }
                            const u32 next = static_cast<u32>(nestedNamespaces_.size()) + 1u;
                            ns = nestedNamespaces_.emplace(renderInstancePath_, next).first;
                        }
                        nestSalt_ = ns->second;
                        backdropLimit_ = i;
                        backdropFrom_ = rl.adjustScope == 1 ? lastDrawOrder : 0u;
                        backdropOnly_.clear();
                        if (rl.adjustScope == 2) backdropOnly_.push_back(l->adjustmentTargets[target]);
                        backdropBare_ = bare || rl.adjustScope != 0;
                        ++prepareDepth_;
                        prepare(comp, project, FrameIndex{frame}, media, imageLookup, imageCtx, settings, frameNumber,
                                playDirection, decodeMode, speed, *snap);
                        --prepareDepth_;
                        nestSalt_ = savedSalt;
                        renderInstancePath_.resize(savedPathSize);
                        frameNumber_ = frameNumber;
                        out.videoLayers += snap->videoLayers;
                        out.staleVideoFrames += snap->staleVideoFrames;
                        out.missingVideoFrames += snap->missingVideoFrames;
                        out.videoSourceWaits.insert(out.videoSourceWaits.end(),
                            snap->videoSourceWaits.begin(), snap->videoSourceWaits.end());
                        const u32 index = static_cast<u32>(out.nested.size());
                        out.nested.push_back(std::move(snap));
                        made.push_back({{frame, bare}, index});
                        return index;
                    };
                    RenderLayer::AdjustTime& at = rl.adjustTime[target];
                    if (ask.replaced) at.now = backdropAt(ask.shown, 0, false);
                    const f64 base = std::floor(ask.shown + 1e-6);
                    const f64 frac = ask.shown - base;
                    if (ask.frameBlend >= 1 && frac > 0.01 && frac < 0.99) {
                        at.next = backdropAt(base + 1.0, 1, false);
                        at.blendT = static_cast<f32>(frac);
                        at.blendMode = (ask.frameBlend == 2 && !settings.finalQuality && settings.flowPolicyScale <= 0.25f)
                            ? u8{1} : ask.frameBlend;
                    }
                    if (ask.vector > 0.0f && ask.travel > 1e-8) {
                        if (at.next == ~0u) at.next = backdropAt(base + 1.0, 1, false);
                        at.vectorBlur = static_cast<f32>(std::clamp(ask.vector, 0.0f, 4.0f) * ask.travel);
                    }
                    at.flowBase = ask.flowBase;
                    if (ask.rgb) {
                        at.rgb = true;
                        for (u32 c = 0; c < 3; ++c) at.channel[c] = backdropAt(ask.rgbAt[c], 2 + c, false);
                    }
                    if (ask.historyDelay > 0) {
                        at.hasHistory = true;
                        at.history = backdropAt(static_cast<f64>(time.value - ask.historyDelay), 5, false);
                    }
                    if (ask.bare) at.bare = backdropAt(static_cast<f64>(time.value), 6, true);
                    for (u32 k = 0; k < ask.echo.size(); ++k) {
                        RenderLayer::TemporalSample sample;
                        sample.weight = ask.echo[k].weight;
                        sample.mask = ask.echo[k].mask;
                        sample.time = ask.echo[k].at;
                        sample.nested = backdropAt(ask.echo[k].at, 7 + k, false);
                        at.echo.push_back(sample);
                    }
                    // Aprimorar: a composição abaixo nos quadros vizinhos. Fora
                    // da composição (antes do 0) não há vizinho: o lado some.
                    for (u32 k = 0; k < ask.neighbors.size(); ++k) {
                        const f64 at0 = static_cast<f64>(time.value + ask.neighbors[k]);
                        if (at0 < 0.0) continue;
                        RenderLayer::NeighborSample nb;
                        nb.offset = ask.neighbors[k];
                        nb.time = at0;
                        nb.nested = backdropAt(at0, 0x40 + k, false);
                        if (nb.nested != ~0u) at.neighbors.push_back(std::move(nb));
                    }
                }
            }
            const bool timeLive = std::any_of(rl.adjustTime.begin(), rl.adjustTime.end(),
                [](const RenderLayer::AdjustTime& t) { return t.active(); });
            if (adjustPlan.empty() && !adjustPlan.hasFold && !timeLive) {   // nenhum efeito vivo: não muda nada
                adjustPlan.clear();
                rl.adjustTime.clear();
                if (rl.maskCount > 0) out.maskData.resize(rl.maskFirst);
                continue;
            }
            groupOpen = false;   // fecha o grupo 3D: o que vem acima vê o ajuste
            // O ajuste de tudo abaixo vira um desenho: um escopo 1 acima dele
            // não tem camada para ajustar (a montagem faz o mesmo).
            if (rl.adjustScope == 0) lastDrawOrder = ~0u;
            out.layers.push_back(std::move(rl));
            ++used;
            continue;
        }

        // Grupo que a câmera de fora atravessa: a filha já sai projetada pela
        // câmera daqui (com o transform do grupo dentro dela) — a camada entra
        // chapada, sem transform próprio.
        bool cameraThrough = false;
        bool particularSheet = false;   // camada do Particular (ver LayerPlacement::particleSpace)
        bool sceneFx = false;           // modelo 3D com efeitos de camada (ver LayerKind::Model3D)
        switch (l->kind) {
            case LayerKind::Video: {
                const Asset* asset = project.asset(l->source);
                if (!asset || !asset->has_video()) continue;
                rl.source.kind = LayerSource::Kind::Video;
                rl.source.width = asset->video.width;
                rl.source.height = asset->video.height;
                ++out.videoLayers;
                break;
            }
            case LayerKind::Image: {
                const ImagePixels* px = imageLookup ? imageLookup(imageCtx, l->source) : nullptr;
                if (!px || !px->width || !px->height) { mark_incomplete(__FILE__, __LINE__); continue; }
                rl.source.kind = LayerSource::Kind::Image;
                rl.source.image = l->source;
                rl.source.pixels = px;
                rl.source.width = px->width;
                rl.source.height = px->height;
                // RIG 2D: a malha presa aos ossos, na pose do instante. Na
                // montagem (todos os ângulos 0) a imagem sai intacta pelo
                // caminho de sempre. A textura da camada vira a CAIXA da pose
                // (um braço pode sair do retângulo da imagem) e a origem da
                // camada não anda (srcShift, como na camada vetorial).
                if (l->rig.active() && (settings.finalQuality || rigSetupLayer_.load(std::memory_order_relaxed) != id.pack())) {
                    std::vector<f32> deg;
                    rig::sample_angles(*l, local, deg);
                    bool posed = false;
                    for (f32 a : deg) posed = posed || std::fabs(a) > 1e-4f;
                    if (posed) {
                        std::vector<rig::Affine> bones;
                        rig::pose(l->rig, deg, bones);
                        rig::SkinMesh mesh;
                        rig::build_skin(l->rig, static_cast<f32>(px->width), static_cast<f32>(px->height), 32, mesh);
                        std::vector<Vec2> moved;
                        rig::deform(mesh, bones, moved);
                        Vec2 mn{1e30f, 1e30f}, mx{-1e30f, -1e30f};
                        for (Vec2 q : moved) {
                            mn = Vec2{std::min(mn.x, q.x), std::min(mn.y, q.y)};
                            mx = Vec2{std::max(mx.x, q.x), std::max(mx.y, q.y)};
                        }
                        mn = mn - Vec2{1.0f, 1.0f};
                        mx = mx + Vec2{1.0f, 1.0f};
                        if (!mesh.tris.empty() && mx.x - mn.x < 16384.0f && mx.y - mn.y < 16384.0f) {
                            rl.source.rigImageW = px->width;
                            rl.source.rigImageH = px->height;
                            rl.source.width = static_cast<u32>(std::max(1.0f, std::ceil(mx.x - mn.x)));
                            rl.source.height = static_cast<u32>(std::max(1.0f, std::ceil(mx.y - mn.y)));
                            srcShift = Vec2{-mn.x, -mn.y};
                            rl.source.rigFirst = static_cast<u32>(out.vec.size());
                            rl.source.rigCount = static_cast<u32>(mesh.tris.size());
                            out.vec.reserve(out.vec.size() + mesh.tris.size());
                            for (u32 vi : mesh.tris)
                                out.vec.push_back(Vec4{moved[vi].x - mn.x, moved[vi].y - mn.y, mesh.uv[vi].x, mesh.uv[vi].y});
                        }
                    }
                }
                break;
            }
            case LayerKind::Shape: {
                ShapeData animated;
                const ShapeData* shp = &l->shape;
                // Tamanho, raio, lados, raio interno e contorno animáveis
                // (TrackProperty::ShapeParam, os índices de ShapeSetParam).
                if (l->shape.shapeType != kShapeVector) {
                    auto pick = [&](u32 p, f32& dst, f32 lo, f32 hi, bool round = false) {
                        const Track* tr = l->tracks.find(TrackProperty::ShapeParam, 0, p);
                        if (!tr) return;
                        if (shp != &animated) { animated = l->shape; shp = &animated; }
                        // Empty tracks retain the source's centering after
                        // its final key is frozen, without rebasing anchors.
                        if (tr->keys.empty()) return;
                        const f32 v = tr->sample(local);
                        dst = std::clamp(round ? std::round(v) : v, lo, hi);
                    };
                    animated = l->shape;
                    ShapeData& a = animated;
                    pick(1, a.cornerRadius, 0.0f, 100000.0f);
                    pick(2, a.points, 1.0f, 64.0f, true);
                    pick(3, a.innerRadius, 0.05f, 0.95f);
                    pick(4, a.strokeWidth, 0.0f, 500.0f);
                    pick(5, a.bounds.w, 1.0f, 16384.0f);
                    pick(6, a.bounds.h, 1.0f, 16384.0f);
                    // Parâmetros das formas paramétricas (7..14): a faixa é a de
                    // shape::clamp_param. Não mexem na caixa (sem srcShift).
                    bool paramAnimated = false;
                    for (u32 p = shape::kParamDepth; p < shape::kParamTotal; ++p) {
                        const Track* tr = l->tracks.find(TrackProperty::ShapeParam, 0, p);
                        if (!tr || tr->keys.empty()) continue;
                        if (f32* dst = shape::param_field(a, p)) {
                            *dst = shape::clamp_param(p, tr->sample(local));
                            paramAnimated = true;
                        }
                    }
                    if (shp == &animated) {
                        // O centro da forma continua na posição da camada
                        // mesmo com o tamanho animado (a âncora é parada).
                        srcShift = Vec2{a.bounds.w * 0.5f - l->transform.anchor.x, a.bounds.h * 0.5f - l->transform.anchor.y};
                    } else if (paramAnimated) {
                        shp = &animated;
                    }
                }
                const ShapeData& sh = *shp;
                if (sh.shapeType == kShapeVector) {
                    // Densidade provável na tela (a mesma conta do texelScale
                    // adiante): tolerância do achatamento e largura do AA.
                    const Mat4 wm = world_2d_frac(comp, *l, static_cast<f64>(time.value));
                    const f32 want = std::min(max_scale(wm), 4.0f) * previewFactor;
                    f32 dens = 1.0f;
                    while (dens < want && dens < 4.0f) dens *= 2.0f;
                    dens = std::max(dens, texel_scale_for(want));
                    const f64 lf = static_cast<f64>(local.value);
                    std::vector<VectorGroup> ev;
                    ev.reserve(sh.vector.groups.size());
                    for (u32 gi = 0; gi < sh.vector.groups.size(); ++gi) ev.push_back(vector::evaluate_group(sh.vector.groups[gi], l->tracks, gi, lf));
                    // Progresso do desenho: o efeito corta os contornos aqui, na malha.
                    const vector::DrawProgress draw = vector_draw_progress(*l, effects_, lf);
                    const u64 key = vector::content_hash(ev, lf, draw) ^ (static_cast<u64>(dens * 64.0f) * 0x9E3779B97F4A7C15ull);
                    VectorCacheEntry& ce = vectorCache_[rid.pack()];
                    if (ce.key != key || ce.lastFrame == 0) {
                        ++heavyStats_.vectorTessellations;
                        vector::VectorMesh mesh;
                        vector::build_mesh(ev, lf, dens, mesh, draw);
                        ce.verts = std::move(mesh.verts);
                        ce.paints = std::move(mesh.paints);
                        ce.min = mesh.min;
                        ce.max = mesh.max;
                        ce.key = key;
                    } else {
                        ++heavyStats_.vectorCacheHits;
                    }
                    ce.lastFrame = frameNumber + 1;
                    if (ce.verts.empty()) continue;
                    // Textura = caixa do conteúdo (+1 px); a camada continua na origem dela.
                    const Vec2 mn = ce.min - Vec2{1.0f, 1.0f}, mx = ce.max + Vec2{1.0f, 1.0f};
                    rl.source.kind = LayerSource::Kind::Vector;
                    rl.source.width = static_cast<u32>(std::max(1.0f, std::ceil(mx.x - mn.x)));
                    rl.source.height = static_cast<u32>(std::max(1.0f, std::ceil(mx.y - mn.y)));
                    srcShift = Vec2{-mn.x, -mn.y};
                    rl.source.vecFirst = static_cast<u32>(out.vec.size());
                    rl.source.vecCount = static_cast<u32>(ce.verts.size() / 2);
                    out.vec.reserve(out.vec.size() + ce.verts.size() + ce.paints.size());
                    for (usize vi = 0; vi + 1 < ce.verts.size(); vi += 2) {
                        const Vec4 a = ce.verts[vi];
                        out.vec.push_back(Vec4{a.x - mn.x, a.y - mn.y, a.z, a.w});
                        out.vec.push_back(ce.verts[vi + 1]);
                    }
                    rl.source.paintFirst = static_cast<u32>(out.vec.size());
                    out.vec.insert(out.vec.end(), ce.paints.begin(), ce.paints.end());
                    break;
                }
                const bool fill = sh.filled && sh.fillColor.w > 0.0f;
                const bool stroke = sh.strokeWidth > 0.0f && sh.strokeColor.w > 0.0f;
                if (!fill && !stroke) continue;   // nada a desenhar
                auto premul = [](Vec4 c) {
                    return Vec4{srgb_to_linear(c.x) * c.w, srgb_to_linear(c.y) * c.w, srgb_to_linear(c.z) * c.w, c.w};
                };
                rl.source.width = static_cast<u32>(std::max(1.0f, sh.bounds.w));
                rl.source.height = static_cast<u32>(std::max(1.0f, sh.bounds.h));
                if (sh.shapeType == 0 && sh.cornerRadius <= 0.0f && !stroke) {
                    // Retângulo reto sem contorno: um clear basta (o caminho mais barato).
                    rl.source.kind = LayerSource::Kind::Solid;
                    rl.source.solid = premul(sh.fillColor);
                    break;
                }
                rl.source.kind = LayerSource::Kind::Shape;
                rl.source.shapeType = sh.shapeType;
                rl.source.shapeFill = premul(sh.fillColor);
                rl.source.shapeStroke = premul(sh.strokeColor);
                rl.source.shapeStrokeWidth = stroke ? sh.strokeWidth : 0.0f;
                {
                    const shape::SdfParams sp = shape::sdf_params(sh, fill, rl.source.shapeStrokeWidth);
                    rl.source.shapeParams = sp.shape;
                    rl.source.shapeExtra = sp.extra;
                    rl.source.shapeMore = sp.more;
                    rl.source.shapeBlob = sp.blob;
                }
                break;
            }
            case LayerKind::Model3D: {
                std::shared_ptr<const scene3d::SceneAsset> asset =
                    modelLookup_ ? modelLookup_(modelCtx_, l->model.scene) : nullptr;
                if (!asset) { mark_incomplete(__FILE__, __LINE__); continue; }
                scene3d::SceneInstance inst;
                fill_object_environment(*l, inst);
                // Matriz 3D para qualquer pai (num pai 2D comum ela é a mesma
                // da 2D): o mesmo mundo que world_3d e o parentesco usam.
                if (can_share_static_pose(*l, *asset)) inst.sharedPose = static_model_pose(asset);
                if (inst.sharedPose) inst.world = world_3d_frac(comp, *l, static_cast<f64>(time.value)) * layer_from_model(l->model);
                else place_model(comp, *l, *asset, static_cast<f64>(time.value), inst, &poseWorkspace_);
                inst.layerKey = rid.pack();
                // Trocar textura 3D: a imagem de cada parte, já na GPU.
                if (scene3d::has_part_textures(*l) && imageLookup) {
                    std::vector<scene3d::PartTexture> swaps;
                    const f64 materialTime = static_cast<f64>(time.value) - static_cast<f64>(l->start.value)
                                           + static_cast<f64>(l->offset.value);
                    scene3d::collect_part_textures(*asset, *l, materialTime, swaps);
                    for (const scene3d::PartTexture& swap : swaps) {
                        const TextureHandle tex = scene_image_texture(
                            swap.image, imageLookup(imageCtx, AssetId::unpack(swap.image)), frameNumber);
                        if (tex.valid()) inst.nodeTextures.push_back(scene3d::SceneNodeTexture{swap.node, tex, swap.repeat});
                    }
                }
                inst.motionBlur = l->motionBlur;
                inst.castShadows = l->model.castShadows;
                inst.doubleSided = model_interior_visible(l->model.interior, asset->shapeParts);
                inst.assetKey = l->model.scene.pack();
                inst.asset = std::move(asset);
                // EFEITOS DA CAMADA (Raios de luz, Brilho, Reflexo de lente...)
                // num modelo / texto 3D / forma 3D: valem sobre o RESULTADO 3D
                // desta camada. Ela ganha uma cena só dela (do tamanho da
                // composição: px da camada = px da composição, como a camada de
                // ajuste) e os efeitos rodam nessa imagem. Antes o grupo saía
                // sem plano e os efeitos não faziam nada. Layout de texto/forma
                // 3D, animadores e ajudantes são identidade no plano: sem efeito
                // de imagem, o modelo continua no grupo compartilhado como antes.
                if (effects_ && !l->effects.empty() && l->modelEffects) {
                    if (out.plans.size() <= used) out.plans.emplace_back();
                    LayerPlacement pl;
                    pl.compFromLayer = Mat4::identity();
                    pl.compWidth = pl.layerWidth = out.compWidth;
                    pl.compHeight = pl.layerHeight = out.compHeight;
                    EffectGraph::plan(*l, *effects_, local, previewFactor, pl, this, out.plans[used], fps, &comp);
                    sceneFx = !out.plans[used].empty() || out.plans[used].hasFold;
                    if (!sceneFx) out.plans[used].clear();
                }
                if (groupOpen && !out.scenes.empty() && !sceneFx && !clipInvolved) {
                    out.scenes.back().instances.push_back(std::move(inst));
                    continue;   // entra no grupo aberto; nenhuma layer nova na pilha
                }
                out.scenes.emplace_back();
                out.scenes.back().instances.push_back(std::move(inst));
                rl.source.kind = LayerSource::Kind::Scene3D;
                rl.source.sceneGroup = static_cast<u32>(out.scenes.size() - 1);
                rl.source.width = out.compWidth;
                rl.source.height = out.compHeight;
                rl.compFromLayer = Mat4::identity();
                rl.texelScale = previewFactor;
                // Com efeitos a cena é só desta camada: o 3D seguinte abre outra
                // (os efeitos não podem pegar os vizinhos).
                groupOpen = !sceneFx && !clipInvolved;
                break;
            }
            case LayerKind::Camera:
            case LayerKind::Light:
                continue;   // não desenham; entram na cena depois do laço
            case LayerKind::Text: {
                TextData captionText;
                TrackSet captionTracks;
                const TextData* textData = &l->text;
                const TrackSet* textTracks = &l->tracks;
                if (!l->captions.empty()) {
                    const auto* segment = text::active_caption(l->captions, local.value);
                    if (!segment) continue;
                    captionText = l->text;
                    // CAIXA ALTA aqui, não no texto guardado: ligar/desligar a
                    // opção (ou aplicar um preset que a muda) vale na hora, e o
                    // texto original continua intacto para editar.
                    captionText.content = l->captionOptions.uppercase ? text::upper_text(segment->text) : segment->text;
                    captionTracks = l->tracks;
                    std::vector<i64> starts;
                    for (const auto& word : segment->words) starts.push_back(word.start);
                    TextData timedText;
                    TrackSet timedTracks;
                    text::apply_caption_animation(l->captionOptions, timedText, timedTracks, starts, segment->end);
                    const u32 animatorBase = static_cast<u32>(captionText.animators.size());
                    captionText.animators.insert(captionText.animators.end(), timedText.animators.begin(), timedText.animators.end());
                    for (u32 i = 0; i < timedTracks.size(); ++i) {
                        Track track = timedTracks.at(i);
                        track.effectIndex += animatorBase;
                        captionTracks.add(std::move(track));
                    }
                    textData = &captionText; textTracks = &captionTracks;
                }
                TextData evaluatedTracking;
                if (const Track* tracking = textTracks->find(TrackProperty::TextTracking); tracking && tracking->driven()) {
                    evaluatedTracking = *textData;
                    evaluatedTracking.tracking = std::clamp(tracking->value_or(local, textData->tracking), -1000.f, 1000.f);
                    textData = &evaluatedTracking;
                }
                const auto font = text::FontManager::instance().font_for(*textData);
                if (!font || textData->content.empty() || (textData->color.w <= 0.0f && textData->strokeWidth <= 0.0f)) continue;
                const TextData& T = *textData;
                // Animadores de CAMADA por letra/palavra/linha entram no mesmo GlyphAnim.
                const bool unitAnim = layeranim::has_units(*l);
                const bool textEffect = effects_ && text::has_transform_effect(*l);
                const bool animatorEffect = text::has_animator_effect(*l);
                const bool animated = text::has_animators(T) || unitAnim || textEffect || animatorEffect;
                // O contorno cabe na distância do atlas (16 px da base × escala).
                const f32 strokeMax = text::kGlyphSpread * std::max(1.0f, T.size) / text::kGlyphBasePx - 1.0f;
                const f32 stroke = std::clamp(T.strokeWidth, 0.0f, std::max(0.0f, strokeMax));
                // Margem: contorno, fundo, sombra e o quanto os animadores podem
                // levar uma letra para fora da caixa (maior valor parado/keyframe).
                f32 pad = stroke > 0.0f ? stroke + 2.0f : 2.0f;
                if (T.background) pad = std::max(pad, std::max(0.0f, T.backgroundPadding) + 2.0f);
                if (T.shadow) {
                    pad = std::max(pad, std::max(std::fabs(T.shadowOffset.x), std::fabs(T.shadowOffset.y)) + std::max(0.0f, T.shadowBlur) + stroke + 2.0f);
                }
                if (animated) {
                    auto maxAbs = [&](u32 ai, u32 p, f32 v) {
                        f32 m = std::fabs(v);
                        if (const Track* tr = textTracks->find(TrackProperty::TextAnimParam, ai, p)) {
                            for (const Keyframe& k : tr->keys) m = std::max(m, std::fabs(k.value));
                            // Expressão: o limite dos keyframes não vale; mede o valor do quadro.
                            if (tr->has_expression()) m = std::max(m, std::fabs(tr->value_or(local, v)));
                        }
                        return m;
                    };
                    f32 extra = 0;
                    for (u32 ai = 0; ai < T.animators.size(); ++ai) {
                        const TextAnimator& a = T.animators[ai];
                        if (!a.enabled) continue;
                        if (a.props & kTextPropPosition) extra += std::max({maxAbs(ai, text::kPosX, a.position.x), maxAbs(ai, text::kPosY, a.position.y), maxAbs(ai, text::kPosZ, a.position.z) * 0.5f});
                        if (a.props & kTextPropScale) extra += T.size * std::max(0.0f, std::max(maxAbs(ai, text::kScaleX, a.scale.x), maxAbs(ai, text::kScaleY, a.scale.y)) / 100.0f - 1.0f);
                        if (a.props & kTextPropRotation) extra += T.size * 0.5f;
                        if (a.props & kTextPropBlur) extra += maxAbs(ai, text::kBlur, a.blur) * 2.0f;
                        if (a.props & kTextPropStrokeWidth) extra += std::min(std::max(0.0f, strokeMax), maxAbs(ai, text::kStrokeWidth, a.strokeWidth));
                        if (a.props & kTextPropTracking) extra += maxAbs(ai, text::kTracking, a.tracking) * static_cast<f32>(std::min<usize>(T.content.size(), 200));
                    }
                    pad += std::min(extra, 4000.0f);
                }
                if (unitAnim) pad += layeranim::glyph_padding(*l, T.size, T.content.size());
                if (animatorEffect) pad += text::animator_effect_padding(*l, local.value, T.size, static_cast<u32>(T.content.size()));
                if (textEffect) {
                    const auto extent = text::measure(*font, T);
                    pad += text::transform_padding(*l, *effects_, static_cast<f64>(local.value), std::max(extent.width, extent.height));
                }
                // Deslocamento de caractere troca as letras antes do layout.
                TextData shaped;
                const TextData* src = &T;
                if (animated) {
                    const std::string c = text::apply_char_offset(T, *textTracks, static_cast<f64>(local.value), fps);
                    if (c != T.content) { shaped = T; shaped.content = c; src = &shaped; }
                }
                text::TextLayout L;
                if (!text::layout_quads(*font, *src, pad, L)) continue;
                {
                    const f32 s = pad - (T.strokeWidth > 0.0f ? T.strokeWidth + 2.0f : 2.0f);   // a mesma margem de recenter_text
                    srcShift = l->captions.empty() ? Vec2{s, s} : Vec2{L.width * .5f - l->transform.anchor.x, L.height * .5f - l->transform.anchor.y};
                }
                rl.source.kind = LayerSource::Kind::Text;
                rl.source.width = static_cast<u32>(std::ceil(L.width));
                rl.source.height = static_cast<u32>(std::ceil(L.height));
                rl.source.glyphFirst = static_cast<u32>(out.glyphs.size());
                // Perspectiva do 3D por caractere: a distância da câmera padrão.
                rl.source.textPersp = Vec4{L.width * 0.5f, L.height * 0.5f, static_cast<f32>(std::max(1u, comp.height())) * 1.2f, 0};
                auto lin = [](Vec4 c) { return Vec4{srgb_to_linear(c.x), srgb_to_linear(c.y), srgb_to_linear(c.z), c.w}; };
                const Vec4 strokeCol = lin(T.strokeColor);
                std::vector<text::GlyphUnits> units(L.quads.size());
                for (usize g = 0; g < L.quads.size(); ++g) units[g] = text::GlyphUnits{L.quads[g].charIndex, L.quads[g].wordIndex, L.quads[g].lineIndex};
                // Desfoque de movimento POR LETRA: um conjunto de glifos por instante do obturador.
                u32 sets = 1;
                ShutterPlan textShutter;
                if (animated && l->motionBlur && comp.motion_blur().enabled && comp.motion_blur().shutterAngle > 0.0f) {
                    const MotionBlurSettings& mb = comp.motion_blur();
                    textShutter = shutter_window(mb, l->transform.motionBlurAmount);
                    if (textShutter.duration > 0)
                        sets = capped_shutter_samples(mb, settings.finalQuality, effective_quality(settings).motionBlurSamples, 0, true);
                }
                // Texto no caminho: cada letra vai para o seu ponto do caminho-guia
                // (pelo centro do avanço), girada pela tangente. O animador age
                // antes, no lugar de origem da letra.
                std::vector<Mat4> onPath;
                if (T.pathLayer != 0 || T.pathMask != kInvalidIndex) {
                    vector::Contour C;
                    bool havePath = false;
                    const Mask* pathMask = nullptr;
                    for (const Mask& m : l->masks) if (m.id == T.pathMask) { pathMask = &m; break; }
                    if (pathMask) {
                        // Máscara desta camada: os pontos já estão no espaço da camada.
                        std::vector<MaskPoint> points;
                        std::vector<Vec4> edges;
                        mask::evaluate_path(*pathMask, static_cast<f64>(local.value), points);
                        mask::flatten(points, pathMask->closed, 0.1f, srcShift, edges);
                        C.closed = pathMask->closed;
                        for (const Vec4& e : edges) C.pts.push_back(Vec2{e.x, e.y});
                        if (!pathMask->closed && !edges.empty()) C.pts.push_back(Vec2{edges.back().z, edges.back().w});
                        havePath = C.pts.size() >= 2;
                    } else if (T.pathMask == kInvalidIndex && T.pathLayer != 0) {
                        const Layer* guide = comp.layer(LayerId::unpack(T.pathLayer));
                        if (guide && guide != l && guide->kind == LayerKind::Shape && guide->shape.shapeType == kShapeVector
                            && vector::guide_contour(guide->shape.vector, guide->tracks, static_cast<f64>(guide->local_time(time).value), C)) {
                            auto aff = [](const Mat4& mm) { return vector::Affine2{mm.col[0].x, mm.col[0].y, mm.col[1].x, mm.col[1].y, mm.col[3].x, mm.col[3].y}; };
                            // Guia → composição → camada de texto → textura do texto.
                            const vector::Affine2 toText = aff(layer_world_matrix(comp, *l, time)).inverse() * aff(layer_world_matrix(comp, *guide, time));
                            for (Vec2& p : C.pts) p = toText.apply(p) + srcShift;
                            havePath = true;
                        }
                    }
                    if (havePath) {
                        f32 base0 = 0.0f;
                        for (const text::GlyphQuad& q : L.quads) if (q.lineIndex == 0) { base0 = q.baseline; break; }
                        // Margens animáveis (trilhas próprias; parado = os valores da camada).
                        auto margin = [&](TrackProperty p, f32 fallback) {
                            const Track* tr = textTracks->find(p);
                            const f32 v = tr && tr->driven() ? tr->value_or(local, fallback) : fallback;
                            return std::isfinite(v) ? std::clamp(v, -100000.0f, 100000.0f) : fallback;
                        };
                        const f32 firstMargin = margin(TrackProperty::TextPathFirstMargin, T.pathOffset);
                        const f32 lastMargin = margin(TrackProperty::TextPathLastMargin, T.pathLastMargin);
                        // Centro de cada letra ao longo do caminho, linha a linha.
                        std::vector<f64> along(L.quads.size(), 0.0);
                        if (T.pathLegacyLayout) {
                            for (usize g = 0; g < L.quads.size(); ++g)
                                along[g] = static_cast<f64>(firstMargin) + static_cast<f64>(L.quads[g].penX + L.quads[g].advance * 0.5f - L.pad);
                        } else {
                            const f64 pathLength = vector::length_of(C);
                            const u32 lines = std::max(1u, L.lines);
                            std::vector<f32> lo(lines, 1e30f), hi(lines, -1e30f);
                            for (const text::GlyphQuad& q : L.quads) {
                                const u32 li = std::min(q.lineIndex, lines - 1);
                                lo[li] = std::min(lo[li], q.penX);
                                hi[li] = std::max(hi[li], q.penX + q.advance);
                            }
                            std::vector<f32> xs;
                            std::vector<usize> members;
                            std::vector<f64> placed;
                            for (u32 li = 0; li < lines; ++li) {
                                if (lo[li] > hi[li]) continue;
                                xs.clear(); members.clear();
                                for (usize g = 0; g < L.quads.size(); ++g) {
                                    if (std::min(L.quads[g].lineIndex, lines - 1) != li) continue;
                                    members.push_back(g);
                                    xs.push_back(L.quads[g].penX + L.quads[g].advance * 0.5f - lo[li]);
                                }
                                placed.assign(xs.size(), 0.0);
                                vector::path_line_positions(pathLength, firstMargin, lastMargin, T.alignment, T.pathForceAlign,
                                                            xs.data(), xs.size(), hi[li] - lo[li], placed.data());
                                for (usize i = 0; i < members.size(); ++i) along[members[i]] = placed[i];
                            }
                        }
                        onPath.assign(L.quads.size(), Mat4::identity());
                        for (usize g = 0; g < L.quads.size(); ++g) {
                            const text::GlyphQuad& q = L.quads[g];
                            const f32 xc = q.penX + q.advance * 0.5f;
                            Vec2 pos;
                            f32 ang = 0.0f;
                            if (!vector::place_on_path_at(C, along[g], T.pathReverse, T.pathPerpendicular, q.baseline - base0, pos, ang)) {
                                onPath.clear();
                                break;
                            }
                            onPath[g] = Mat4::translation(Vec3{pos.x, pos.y, 0}) * Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, ang * kDeg2Rad))
                                      * Mat4::translation(Vec3{-xc, -q.baseline, 0});
                        }
                    }
                }
                auto pathM = [&](usize g) { return onPath.empty() ? Mat4::identity() : onPath[g]; };
                // A span or a fit-to-box layout can make individual glyphs
                // smaller than T.size. Their distance field shrinks too; a
                // wider stroke would turn the entire atlas cell into a box.
                auto glyphStroke = [&](const text::GlyphQuad& q, const text::GlyphAnim& a) {
                    return std::clamp(stroke + a.strokeAdd, 0.0f, std::max(0.0f, text::kGlyphSpread * q.k - 1.0f));
                };
                const bool outlinePass = stroke > 0 || animatorEffect || std::any_of(T.animators.begin(), T.animators.end(),
                    [](const TextAnimator& a) { return a.enabled && (a.props & kTextPropStrokeWidth); });
                std::vector<text::GlyphAnim> anim;
                const Vec4 emptyBounds{1e30f, 1e30f, -1e30f, -1e30f};
                std::vector<Vec4> wordBounds(std::max(1u, L.words), emptyBounds), lineBounds(std::max(1u, L.lines), emptyBounds);
                for (const auto& q : L.quads) {
                    auto extend = [&](Vec4& b) { b.x = std::min(b.x, q.x0); b.y = std::min(b.y, q.y0); b.z = std::max(b.z, q.x1); b.w = std::max(b.w, q.y1); };
                    if (q.wordIndex < wordBounds.size()) extend(wordBounds[q.wordIndex]);
                    if (q.lineIndex < lineBounds.size()) extend(lineBounds[q.lineIndex]);
                }
                auto glyphMatrix = [&](const text::GlyphQuad& q, const text::GlyphAnim& a) {
                    f32 x0 = q.x0, x1 = q.x1, y0 = q.y0, y1 = q.y1;
                    const Vec4* group = a.anchorGrouping == 1 && q.wordIndex < wordBounds.size() ? &wordBounds[q.wordIndex]
                                  : a.anchorGrouping == 2 && q.lineIndex < lineBounds.size() ? &lineBounds[q.lineIndex] : nullptr;
                    if (group) {
                        x0 = group->x; y0 = group->y; x1 = group->z; y1 = group->w;
                    }
                    const Vec3 pivot{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0};
                    Mat4 m = Mat4::translation(pivot + a.translate + Vec3{a.trackingShift, 0, 0});
                    if (a.rotation.x != 0 || a.rotation.y != 0 || a.rotation.z != 0)
                        m = m * Mat4::from_quat(Quat::from_euler_zyx(a.rotation.x * kDeg2Rad, a.rotation.y * kDeg2Rad, a.rotation.z * kDeg2Rad));
                    m = m * a.skewTransform;
                    return m * Mat4::scale(Vec3{a.scale.x, a.scale.y, 1}) * Mat4::translation(-pivot);
                };
                std::vector<Mat4> textTransforms;
                auto evaluateGlyphs = [&](f64 lt) {
                    text::evaluate_text_animators(T, *textTracks, lt, fps, units, L.chars, L.words, L.lines, anim);
                    if (animatorEffect) text::evaluate_animator_effects(*l, lt, fps, units, L.chars, L.words, L.lines, anim);
                    if (unitAnim) {
                        const f32 align = T.alignment == 1 ? .5f : T.alignment == 2 ? 1.f : 0.f;
                        layeranim::apply_to_glyphs(*l, lt, fps, units, L.chars, L.words, L.lines, align, anim);
                    }
                    if (textEffect) text::evaluate_transform_effects(*l, *effects_, lt, L, anim, textTransforms);
                };
                if (sets > 1) {
                    std::vector<std::array<Vec2, 4>> previous(L.quads.size());
                    std::vector<std::array<f64, 4>> distance(L.quads.size());
                    f64 travel = 0;
                    constexpr u32 probes = 17;
                    for (u32 p = 0; p < probes; ++p) {
                        const f64 unit = p == 0 || p + 1 == probes ? static_cast<f64>(p) / (probes - 1)
                            : (p + .23 * std::sin(p * 2.399963229728653)) / (probes - 1);
                        const f64 offset = textShutter.begin + textShutter.duration * unit;
                        evaluateGlyphs(local.value + offset);
                        const f64 t = time.value + offset;
                        const Mat4 world = (wants_3d(comp, *l, time)
                            ? (settings.sceneEditor.enabled && !settings.finalQuality
                                ? scene_editor_projection(out.compWidth, out.compHeight, settings.sceneEditor)
                                : comp_view_projection_frac(comp, t, out.compWidth, out.compHeight)) * world_3d_frac(comp, *l, t)
                            : world_2d_frac(comp, *l, t)) * Mat4::translation(Vec3{-srcShift.x, -srcShift.y, 0});
                        for (usize g = 0; g < L.quads.size(); ++g) {
                            const auto& q = L.quads[g];
                            const Mat4 matrix = world * pathM(g) * (textEffect ? textTransforms[g] : Mat4::identity()) * glyphMatrix(q, anim[g]);
                            const Vec4 corners[4]{{q.x0,q.y0,0,1},{q.x1,q.y0,0,1},{q.x0,q.y1,0,1},{q.x1,q.y1,0,1}};
                            for (u32 c = 0; c < 4; ++c) {
                                const Vec4 v = matrix * corners[c];
                                if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.w) || std::fabs(v.w) < 1e-5f) { travel = 1e6; continue; }
                                const Vec2 point{v.x / v.w, v.y / v.w};
                                if (p) distance[g][c] += (point - previous[g][c]).length() * previewFactor;
                                previous[g][c] = point;
                                travel = std::max(travel, distance[g][c]);
                            }
                        }
                    }
                    sets = capped_shutter_samples(comp.motion_blur(), settings.finalQuality,
                        effective_quality(settings).motionBlurSamples, travel, true);
                }
                // Charge the same frame allowance as 3D samples, including
                // already prepared children. No admission means no allocation.
                const u64 bytesPerSet = (static_cast<u64>(L.quads.size()) * (1u + (T.shadow ? 1u : 0u) + (outlinePass ? 1u : 0u)) + 1u) * sizeof(GlyphInstance);
                const u32 admitted = static_cast<u32>(std::min<u64>(ShutterPlan::kHardLimit,
                    exposureBytesRemaining_ / std::max<u64>(1, bytesPerSet)));
                // O orçamento do quadro é FIXO: recusar aqui deixava o quadro
                // incompleto para sempre e o export morria em 30 s. Usa as
                // amostras que cabem (no mínimo o texto sem desfoque).
                if (sets > admitted) sets = std::max(1u, admitted);
                exposureBytesRemaining_ -= std::min(exposureBytesRemaining_, bytesPerSet * sets);
                textShutter.count = sets;
                for (u32 si = 0; si < sets; ++si) {
                    const f64 lt = static_cast<f64>(local.value) + (sets > 1 ? textShutter.offset(si) : 0.0);
                    evaluateGlyphs(lt);
                    // Composite each instant at full opacity, then average its
                    // premultiplied result. Weighting glyphs changes their overlap.
                    const f32 w = 1.0f;
                    // Fundo (caixa arredondada atrás do texto): uv.x < 0 marca "sólido".
                    if (T.background && T.backgroundColor.w > 0.0f && onPath.empty()) {
                        const f32 bp = std::max(0.0f, T.backgroundPadding);
                        GlyphInstance b;
                        b.rect = Vec4{L.pad - bp, L.pad - bp, L.pad + L.contentWidth + bp, L.pad + L.contentHeight + bp};
                        b.uv = Vec4{-1.0f, std::max(0.0f, T.backgroundRadius), 0, 0};
                        b.fill = lin(T.backgroundColor);
                        b.fill.w *= w;
                        b.misc = Vec4{0, 0, 1, 0};
                        out.glyphs.push_back(b);
                    }
                    // Sombra: os mesmos glifos (com a animação), deslocados, desfocados pelo SDF.
                    if (T.shadow && T.shadowColor.w > 0.0f) {
                        const Vec4 sc = lin(T.shadowColor);
                        for (usize g = 0; g < L.quads.size(); ++g) {
                            const text::GlyphQuad& q = L.quads[g];
                            const text::GlyphAnim& a = anim[g];
                            GlyphInstance gi;
                            gi.rect = Vec4{q.x0, q.y0, q.x1, q.y1};
                            gi.uv = Vec4{q.u0, q.v0, q.u1, q.v1};
                            gi.fill = sc;
                            gi.fill.w *= a.opacity * w;
                            gi.stroke = sc;
                            gi.stroke.w *= a.opacity * w;
                            gi.xform = Mat4::translation(Vec3{T.shadowOffset.x, T.shadowOffset.y, 0}) * pathM(g)
                                * (textEffect ? textTransforms[g] : Mat4::identity()) * glyphMatrix(q, a);
                            gi.misc = Vec4{0, 0, q.k, glyphStroke(q, a)};
                            gi.extra = Vec4{std::max(0.0f, T.shadowBlur) * 0.5f + a.blur, 0, 0, 0};
                            out.glyphs.push_back(gi);
                        }
                    }
                    const usize textGlyphStart = out.glyphs.size();
                    for (usize g = 0; g < L.quads.size(); ++g) {
                        const text::GlyphQuad& q = L.quads[g];
                        const text::GlyphAnim& a = anim[g];
                        GlyphInstance gi;
                        gi.rect = Vec4{q.x0, q.y0, q.x1, q.y1};
                        gi.uv = Vec4{q.u0, q.v0, q.u1, q.v1};
                        Vec4 fill = lin(q.color);
                        if (a.fill.w > 0.0f) {
                            const Vec4 af = lin(Vec4{a.fill.x, a.fill.y, a.fill.z, 1});
                            fill = Vec4{fill.x + (af.x - fill.x) * a.fill.w, fill.y + (af.y - fill.y) * a.fill.w, fill.z + (af.z - fill.z) * a.fill.w, fill.w};
                        }
                        Vec4 sc = strokeCol;
                        if (a.stroke.w > 0.0f) {
                            const Vec4 as = lin(Vec4{a.stroke.x, a.stroke.y, a.stroke.z, 1});
                            sc = Vec4{sc.x + (as.x - sc.x) * a.stroke.w, sc.y + (as.y - sc.y) * a.stroke.w, sc.z + (as.z - sc.z) * a.stroke.w, sc.w};
                        }
                        fill.w *= a.opacity * a.fillOpacity * w;
                        sc.w *= a.opacity * w;
                        gi.fill = fill;
                        gi.stroke = sc;
                        gi.xform = pathM(g) * (textEffect ? textTransforms[g] : Mat4::identity()) * glyphMatrix(q, a);
                        gi.misc = Vec4{0, 0, q.k, glyphStroke(q, a)};
                        gi.extra = Vec4{a.blur, 0, 0, 0};
                        out.glyphs.push_back(gi);
                    }
                    if (outlinePass) {
                        // Draw every outline behind every fill. Interleaving
                        // fill+outline per letter let the next letter's thick
                        // outline erase part of its neighbor (kerning/script).
                        const usize end = out.glyphs.size();
                        for (usize gi = textGlyphStart; gi < end; ++gi) {
                            GlyphInstance fill = out.glyphs[gi];
                            out.glyphs[gi].fill = Vec4{};
                            fill.stroke = Vec4{};
                            fill.misc.w = 0;
                            out.glyphs.push_back(fill);
                        }
                    }
                    if (si == 0) rl.source.glyphCount = static_cast<u32>(out.glyphs.size()) - rl.source.glyphFirst;
                }
                rl.source.glyphSets = sets;
                if (!onPath.empty() || animated) {
                    // Extend animated bounds without changing the original
                    // raster grid: an opacity-only animator must not move the
                    // glyphs to a fractional texel and filter them twice.
                    Vec2 bmin{1e30f, 1e30f}, bmax{-1e30f, -1e30f};
                    for (usize gi = rl.source.glyphFirst; gi < out.glyphs.size(); ++gi) {
                        const GlyphInstance& G = out.glyphs[gi];
                        for (Vec2 c : {Vec2{G.rect.x, G.rect.y}, Vec2{G.rect.z, G.rect.y}, Vec2{G.rect.x, G.rect.w}, Vec2{G.rect.z, G.rect.w}}) {
                            const Vec4 q = G.xform * Vec4{c.x, c.y, 0, 1};
                            bmin = Vec2{std::min(bmin.x, q.x), std::min(bmin.y, q.y)};
                            bmax = Vec2{std::max(bmax.x, q.x), std::max(bmax.y, q.y)};
                        }
                    }
                    if (bmin.x <= bmax.x) {
                        const bool pathBounds = !onPath.empty();
                        const f32 left = pathBounds ? bmin.x - pad : std::floor(std::min(0.f, bmin.x));
                        const f32 top = pathBounds ? bmin.y - pad : std::floor(std::min(0.f, bmin.y));
                        const f64 width = std::ceil(pathBounds ? bmax.x - bmin.x + 2.0 * pad
                            : std::max<f64>(rl.source.width, bmax.x) - left);
                        const f64 height = std::ceil(pathBounds ? bmax.y - bmin.y + 2.0 * pad
                            : std::max<f64>(rl.source.height, bmax.y) - top);
                        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0
                            || width >= std::numeric_limits<u32>::max() || height >= std::numeric_limits<u32>::max()) {
                            mark_incomplete(__FILE__, __LINE__);
                            continue;
                        }
                        const Vec2 S{-left, -top};
                        const Mat4 st = Mat4::translation(Vec3{S.x, S.y, 0});
                        for (usize gi = rl.source.glyphFirst; gi < out.glyphs.size(); ++gi) out.glyphs[gi].xform = st * out.glyphs[gi].xform;
                        rl.source.width = static_cast<u32>(width);
                        rl.source.height = static_cast<u32>(height);
                        srcShift = srcShift + S;
                        rl.source.textPersp.x = pathBounds ? rl.source.width * .5f : rl.source.textPersp.x + S.x;
                        rl.source.textPersp.y = pathBounds ? rl.source.height * .5f : rl.source.textPersp.y + S.y;
                    }
                }
                break;
            }
            case LayerKind::ParticleSystem: {
                // Partículas analíticas: o shader resolve tudo a partir do
                // tempo local — aqui só o bloco de parâmetros.
                // Os keyframes de ParticleParam ja entraram: sem trilha, o valor
                // parado do campo.
                const ParticleData pd = sampled_particles(*l, local);
                if (pd.emitterType == static_cast<u32>(ParticleEmitter::Particular)) {
                    particularSheet = true;
                    // Particular (o sistema do app antigo): a camada é uma
                    // folha transparente do tamanho da composição e o efeito
                    // `aurea.generate.particular` desenha as partículas nela.
                    rl.source.kind = LayerSource::Kind::Solid;
                    rl.source.solid = Vec4{0, 0, 0, 0};
                    rl.source.width = comp.width();
                    rl.source.height = comp.height();
                    break;
                }
                const f32 lw = static_cast<f32>(comp.width()), lh = static_cast<f32>(comp.height());
                const f32 tsec = static_cast<f32>(static_cast<f64>(local.value) / fps);
                const f32 rate = std::clamp(pd.rate, 0.1f, 1000000.0f);   // o shader analítico aceita milhões
                const f32 life = std::clamp(pd.lifetime, 0.05f, 60.0f);
                // Preview: a fração do bloco de qualidade (8E); export = todas.
                const u32 cap = std::max<u32>(1u, static_cast<u32>(static_cast<f64>(pd.maxParticles)
                                                                    * std::clamp(resolve_heavy(settings).particles, 0.05f, 1.0f)));
                // Uma primaria vive `life`; com o estouro, `burst` nascem no
                // instante zero. O aux multiplica as instancias (cada primaria
                // gera `auxCount` secundarias), e o teto de particulas manda.
                const u32 auxMul = 1u + std::min<u32>(pd.auxCount, 16u);
                const u32 flowSlots = std::min<u32>(cap / auxMul,
                                                   static_cast<u32>(std::ceil(rate * life * 1.25f)) + 1u);
                const u32 slots = std::min<u32>(cap / auxMul, flowSlots + pd.burst);
                auto lin = [](Vec4 c) { return Vec4{srgb_to_linear(c.x), srgb_to_linear(c.y), srgb_to_linear(c.z), c.w}; };
                rl.source.kind = LayerSource::Kind::Particles;
                rl.source.width = comp.width();
                rl.source.height = comp.height();
                // Bloco 0..6: o que ja existia (taxa, vida, forcas, cor, origem).
                rl.source.particleBlock[0] = Vec4{rate, life, pd.speed, pd.spread * kDeg2Rad};
                // Gravidade do modelo: y para CIMA (−980 = cai); a tela tem y para baixo.
                rl.source.particleBlock[1] = Vec4{pd.gravity.x, -pd.gravity.y, pd.startSize, pd.endSize};
                rl.source.particleBlock[2] = Vec4{pd.startOpacity, pd.endOpacity, pd.direction * kDeg2Rad, static_cast<f32>(pd.seed % 1000003u)};
                rl.source.particleBlock[3] = Vec4{pd.emitterSize.x, pd.emitterSize.y, tsec, static_cast<f32>(slots)};
                rl.source.particleBlock[4] = lin(pd.startColor);
                rl.source.particleBlock[5] = lin(pd.endColor);
                rl.source.particleBlock[6] = Vec4{lw * 0.5f + pd.emitterOffset.x, lh * 0.5f + pd.emitterOffset.y, 0, 0};
                // Bloco 8..: Aurea Particular.
                rl.source.particleBlock[7] = Vec4{static_cast<f32>(pd.emitterType), pd.emitterRadius,
                                                  pd.emitterRotation * kDeg2Rad, pd.emitterDepth};
                rl.source.particleBlock[8] = Vec4{static_cast<f32>(pd.gridX), static_cast<f32>(pd.gridY),
                                                  pd.emitFill ? 1.0f : 0.0f, static_cast<f32>(pd.burst)};
                rl.source.particleBlock[9] = Vec4{pd.lifeRandom, pd.speedRandom, pd.inheritVelocity,
                                                  static_cast<f32>(pd.particleType)};
                rl.source.particleBlock[10] = Vec4{pd.softness, pd.rotation * kDeg2Rad,
                                                   pd.rotationRandom * kDeg2Rad, pd.spin * kDeg2Rad};
                rl.source.particleBlock[11] = Vec4{pd.drag, pd.wind.x, -pd.wind.y, pd.turbulence};
                rl.source.particleBlock[12] = Vec4{pd.turbulenceScale, pd.turbulenceSpeed,
                                                   pd.vortex * kDeg2Rad, pd.attractor};
                rl.source.particleBlock[13] = Vec4{pd.trailLength, pd.trailTaper,
                                                   static_cast<f32>(pd.auxCount), pd.auxAt};
                rl.source.particleBlock[14] = Vec4{pd.auxLife, pd.auxSpeed, pd.auxSize, pd.auxSpread * kDeg2Rad};
                rl.source.particleBlock[15] = Vec4{static_cast<f32>(pd.collision), pd.collisionY,
                                                   pd.collisionBounce, 0.0f};
                rl.source.particleBlock[16] = lin(pd.auxColor);
                // "Velocity from motion": a velocidade da CAMADA no instante,
                // em px/s — o shader soma uma fracao dela a velocidade inicial.
                {
                    // A posicao da camada sai do MESMO `tracks` que o resto do
                    // renderizador le (nao do campo estatico do Transform): com
                    // keyframe, o campo nao acompanha.
                    const FrameIndex next{local.value + 1};
                    const f32 x0 = l->tracks.sample_or(TrackProperty::PositionX, local, l->transform.position.x);
                    const f32 y0 = l->tracks.sample_or(TrackProperty::PositionY, local, l->transform.position.y);
                    const f32 x1 = l->tracks.sample_or(TrackProperty::PositionX, next, l->transform.position.x);
                    const f32 y1 = l->tracks.sample_or(TrackProperty::PositionY, next, l->transform.position.y);
                    const f32 dt = static_cast<f32>(1.0 / std::max(1.0, fps));
                    rl.source.particleBlock[17] = Vec4{(x1 - x0) / dt, -(y1 - y0) / dt, 0, 0};
                }
                rl.source.particleBlock[18] = Vec4{lw / std::max(1.0f, lh), static_cast<f32>(flowSlots), 0, 0};
                // Quantas instancias sao FLUXO continuo; o resto e estouro.
                rl.source.particleBlock[19] = Vec4{static_cast<f32>(flowSlots), 0, 0, 0};
                // O estouro entra alem das slots do fluxo continuo: com taxa 0
                // (so estouro) `slots` seria 1 e o burst nao teria onde caber.
                rl.source.particleSlots = std::min<u32>(cap, slots * auxMul);
                rl.source.particleAdditive = pd.blendMode == 1;
                // 8.2: fonte de emissão, textura, malha, curvas... (isolado).
                prepare_particle_extras(comp, *l, pd, layerTime, imageLookup, imageCtx, frameNumber, rid, rl.source);
                break;
            }
            case LayerKind::Composition: {
                // Pré-composição: a filha no tempo da FONTE da camada (entrada,
                // velocidade, reverso), convertido para a taxa dela.
                const Composition* child = project.timeline().composition(l->nested.composition);
                if (!child || child == &comp || prepareDepth_ >= 8) continue;
                const f64 childFps = child->fps() > 0.0 ? child->fps() : fps;
                const i64 cf = static_cast<i64>(std::floor(l->source_frame(layerTime) * childFps / fps + 1e-6));
                if (cf < 0 || cf >= child->duration().value) continue;
                auto snapChild = std::make_unique<FrameSnapshot>();
                const u32 savedSalt = nestSalt_;
                const usize savedPathSize = renderInstancePath_.size();
                renderInstancePath_.push_back(id.pack());
                renderInstancePath_.push_back(l->nested.composition.pack());
                auto ns = nestedNamespaces_.find(renderInstancePath_);
                if (ns == nestedNamespaces_.end()) {
                    // The packed format reserves 14 bits for the namespace.
                    // Never wrap and silently alias a still-live decoder/cache.
                    if (nestedNamespaces_.size() >= 0x3FFFu) {
                        renderInstancePath_.resize(savedPathSize);
                        mark_incomplete(__FILE__, __LINE__);
                        continue;
                    }
                    const u32 next = static_cast<u32>(nestedNamespaces_.size()) + 1u;
                    ns = nestedNamespaces_.emplace(renderInstancePath_, next).first;
                }
                nestSalt_ = ns->second;
                ++prepareDepth_;
                RenderSettings childSettings = settings;
                childSettings.sceneEditor.enabled = false;
                const CameraPassThrough savedThrough = g_cameraPassThrough;
                cameraThrough = l->nested.cameraPassThrough && child->width() == comp.width() && child->height() == comp.height()
                             && has_active_camera(comp, time)
                             && !(settings.sceneEditor.enabled && !settings.finalQuality);
                if (cameraThrough) {
                    CameraPassThrough pt;
                    pt.child = child;
                    pt.parent = &comp;
                    pt.group = l;
                    pt.parentTime0 = static_cast<f64>(time.value);
                    pt.childTime0 = static_cast<f64>(cf);
                    pt.rate = childFps > 0.0 ? fps / childFps / std::max(1e-3, std::fabs(static_cast<f64>(l->speed_at(time)))) : 1.0;
                    g_cameraPassThrough = pt;
                }
                prepare(*child, project, FrameIndex{cf}, media, imageLookup, imageCtx, childSettings, frameNumber,
                        playDirection, decodeMode, speed, *snapChild);
                g_cameraPassThrough = savedThrough;
                --prepareDepth_;
                nestSalt_ = savedSalt;
                renderInstancePath_.resize(savedPathSize);
                frameNumber_ = frameNumber;
                out.videoLayers += snapChild->videoLayers;
                out.staleVideoFrames += snapChild->staleVideoFrames;
                out.missingVideoFrames += snapChild->missingVideoFrames;
                out.videoSourceWaits.insert(out.videoSourceWaits.end(),
                    snapChild->videoSourceWaits.begin(), snapChild->videoSourceWaits.end());
                out.culledLayers += snapChild->culledLayers;
                rl.source.kind = LayerSource::Kind::Nested;
                rl.source.width = child->width();
                rl.source.height = child->height();
                rl.source.nestedIndex = static_cast<u32>(out.nested.size());
                out.nested.push_back(std::move(snapChild));
                break;
            }
            default:
                continue;   // partículas: fora desta fase
        }
        if (rl.source.kind == LayerSource::Kind::Scene3D) {
            // O grupo compartilhado não tem efeitos de layer; o modelo com
            // efeitos (sceneFx) já planejou os dele sobre a cena só dele.
            if (out.plans.size() <= used) out.plans.emplace_back();
            if (!sceneFx) out.plans[used].clear();
            out.layers.push_back(std::move(rl));
            ++used;
            lastDrawOrder = i;
            continue;
        }

        // Transform da layer, com a cadeia de pais (cada pai no próprio tempo).
        Mat4 m = cameraThrough ? Mat4::identity() : world_2d_frac(comp, *l, static_cast<f64>(time.value));
        // Particular: a rotação (X/Y/Z, orientação), a posição Z e os pais vão
        // para o espaço 3D das partículas; a folha fica parada na composição.
        const bool inScene3d = !cameraThrough && !particularSheet && wants_3d(comp, *l, time);
        Mat4 particularWorld = Mat4::identity();
        if (particularSheet) {
            particularWorld = world_3d(comp, *l, time);
            m = Mat4::identity();
        }
        if (inScene3d) {
            // Rotação X/Y, profundidade, nulo 3D na cadeia: a MESMA câmera dos
            // modelos 3D (padrão = plano Z=0 1:1 com a composição).
            m = compFromClip * viewProj3d * world_3d(comp, *l, time);
        }
        const Mat4 shiftM = Mat4::translation(Vec3{-srcShift.x, -srcShift.y, 0});
        const bool shifted = srcShift.x != 0.0f || srcShift.y != 0.0f;
        if (shifted) m = m * shiftM;
        Mat4 particleLayerFromComp = Mat4::identity();
        const bool cameraEmitter = !particularSheet && (effectCameraActive || inScene3d)
            && std::any_of(l->effects.begin(), l->effects.end(), [](const EffectInstance& effect) {
                return effect.enabled && effect.type == effect_type_id(effect_keys::kParticular);
            }) && inverse_particle_plane(m, particleLayerFromComp);
        // Transições de entrada/saída: ajuste procedural perto das bordas.
        if (l->transitionIn != 0 || l->transitionOut != 0) {
            const f32 cw = static_cast<f32>(comp.width()), chh = static_cast<f32>(comp.height());
            const Vec4 c4 = m * Vec4{static_cast<f32>(rl.source.width) * 0.5f, static_cast<f32>(rl.source.height) * 0.5f, 0, 1};
            const Vec3 center{c4.w != 0.0f ? c4.x / c4.w : c4.x, c4.w != 0.0f ? c4.y / c4.w : c4.y, 0};
            auto apply = [&](u8 type, f32 u, f32 dirSign) {
                // u: 0 = fora de cena, 1 = no lugar; curva suave (ease in-out).
                const f32 e = u * u * (3.0f - 2.0f * u);
                switch (type) {
                    case 1: rl.opacity *= e; break;
                    case 2: m = Mat4::translation(Vec3{0, (1.0f - e) * chh * 0.25f * dirSign, 0}) * m; rl.opacity *= std::min(1.0f, e * 1.5f); break;
                    case 3: m = Mat4::translation(Vec3{-(1.0f - e) * cw * 0.25f * dirSign, 0, 0}) * m; rl.opacity *= std::min(1.0f, e * 1.5f); break;
                    case 4: {
                        const f32 k = 0.6f + 0.4f * e;
                        m = Mat4::translation(center) * Mat4::scale(Vec3{k, k, 1}) * Mat4::translation(-center) * m;
                        rl.opacity *= e;
                        break;
                    }
                    case 5: {
                        const f32 k = 0.5f + 0.5f * e;
                        const f32 a = (1.0f - e) * 1.5708f * dirSign;
                        m = Mat4::translation(center) * Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, a))
                          * Mat4::scale(Vec3{k, k, 1}) * Mat4::translation(-center) * m;
                        rl.opacity *= e;
                        break;
                    }
                    default: break;
                }
            };
            if (l->transitionIn != 0 && l->transitionInFrames > 0) {
                const f32 u = static_cast<f32>(time.value - l->start.value) / static_cast<f32>(l->transitionInFrames);
                if (u < 1.0f) apply(l->transitionIn, std::clamp(u, 0.0f, 1.0f), 1.0f);
            }
            if (l->transitionOut != 0 && l->transitionOutFrames > 0) {
                const f32 u = static_cast<f32>(l->end.value - 1 - time.value) / static_cast<f32>(l->transitionOutFrames);
                if (u < 1.0f) apply(l->transitionOut, std::clamp(u, 0.0f, 1.0f), -1.0f);
            }
            if (rl.opacity <= 0.0f) continue;
        }
        rl.compFromLayer = m;
        // Densidade na tela: no 2D, a escala da matriz; no 3D, a PROJEÇÃO dos
        // cantos (com a divisão pela profundidade) — perto da câmera a camada
        // aparece maior e precisa de mais texels.
        f32 onScreen = max_scale(m);
        if (inScene3d && rl.source.width > 0 && rl.source.height > 0) {
            const f32 lw = static_cast<f32>(rl.source.width), lh = static_cast<f32>(rl.source.height);
            Vec2 q[4];
            bool ok = true;
            const Vec2 corners[4] = {{0, 0}, {lw, 0}, {lw, lh}, {0, lh}};
            for (int k = 0; k < 4 && ok; ++k) {
                const Vec4 c = m * Vec4{corners[k].x, corners[k].y, 0, 1};
                ok = c.w > 1e-4f;
                if (ok) q[k] = Vec2{c.x / c.w, c.y / c.w};
            }
            if (ok) {
                auto len = [](Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); };
                onScreen = std::max({len(q[0], q[1]) / lw, len(q[3], q[2]) / lw, len(q[0], q[3]) / lh, len(q[1], q[2]) / lh});
            } else {
                onScreen = 4.0f;   // atravessa a câmera: densidade alta
            }
        }
        rl.texelScale = texel_scale_for(std::min(onScreen, 4.0f) * previewFactor);
        // Vetor (texto na GPU, forma SDF): não há "pixel da fonte" — ampliado,
        // a camada é desenhada mais densa (até 4×) e continua nítida.
        if (rl.source.kind == LayerSource::Kind::Text || rl.source.kind == LayerSource::Kind::Shape
            || rl.source.kind == LayerSource::Kind::Vector) {
            f32 k = 1.0f;
            const f32 want = std::min(onScreen, 4.0f) * previewFactor;
            while (k < want && k < 4.0f) k *= 2.0f;
            rl.texelScale = std::max(rl.texelScale, k);
        }
        // Máscaras: o caminho no instante, achatado a 0,2 texel da densidade de
        // trabalho (px da camada → px da fonte: a margem do texto).
        if (!l->masks.empty()) {
            rl.maskFirst = static_cast<u32>(out.maskData.size());
            rl.maskCount = mask::build_block(*l, static_cast<f64>(local.value), srcShift,
                                             0.2f / std::max(rl.texelScale, 1e-3f), out.maskData, rl.maskStart, rl.maskKey);
        }

        // Exposure starts at phase/360 and lasts angle/360 frames. Sample
        // density follows projected motion; quality never shifts this clock.
        ShutterPlan layerShutter = l->motionBlur && !cameraThrough
            ? shutter_window(comp.motion_blur(), l->transform.motionBlurAmount) : ShutterPlan{};
        rl.blurSourceTransform = shifted ? shiftM : Mat4::identity();
        auto shutterMatrix = [&](f64 offset) {
            const f64 ts = static_cast<f64>(time.value) + offset;
            Mat4 matrix = wants_3d(comp, *l, time)
                ? (settings.sceneEditor.enabled && !settings.finalQuality ? scene_editor_projection(out.compWidth, out.compHeight, settings.sceneEditor)
                    : comp_view_projection_frac(comp, ts, out.compWidth, out.compHeight)) * world_3d_frac(comp, *l, ts)
                : world_2d_frac(comp, *l, ts);
            return shifted ? matrix * shiftM : matrix;
        };
        if (l->motionBlur && comp.motion_blur().enabled && !cameraThrough) {
            const MotionBlurSettings& mb = comp.motion_blur();
            // Scene planes are resampled on the group's clock below.
            const f64 travel = shutter_projected_path(layerShutter, static_cast<f32>(rl.source.width),
                static_cast<f32>(rl.source.height), shutterMatrix, previewFactor);
            const u32 k = rl.source.glyphSets > 1 ? rl.source.glyphSets
                : capped_shutter_samples(mb, settings.finalQuality, effective_quality(settings).motionBlurSamples, travel);
            layerShutter.count = k;
            if (layerShutter.duration > 0.0 && k > 1) {
                rl.blurMatrices.resize(k);
                for (u32 i = 0; i < k; ++i) {
                    rl.blurMatrices[i] = shutterMatrix(layerShutter.offset(i));
                }
            }
        }
        // Eco e RGB no tempo (transform 2D com pais, em instantes passados).
        // Eco e rastro: o efeito "Eco e rastro" (parâmetros no instante, com
        // keyframes) manda; sem ele, os campos antigos da camada.
        u32 echoCount = l->echoCount;
        f32 echoDelay = l->echoDelay, echoDecay = l->echoDecay, rgbDelay = l->rgbDelay;
        if (effects_) {
            const EffectTypeId echoType = echoType_;
            const ParameterRegistry* ep = effects_->params(echoType);
            for (const EffectInstance& inst : l->effects) {
                if (!inst.enabled || inst.type != echoType || !ep || ep->count() < 4) continue;
                echoCount = static_cast<u32>(std::clamp(evaluate_param(l->tracks, inst, 0, ep->at(0), local).v[0], 0.0f, 16.0f) + 0.5f);
                echoDelay = evaluate_param(l->tracks, inst, 1, ep->at(1), local).v[0];
                echoDecay = evaluate_param(l->tracks, inst, 2, ep->at(2), local).v[0];
                rgbDelay = evaluate_param(l->tracks, inst, 3, ep->at(3), local).v[0];
                break;
            }
        }
        if ((echoCount > 0 || rgbDelay > 0.0f) && !wants_3d(comp, *l, time)) {
            const f64 t0 = static_cast<f64>(time.value);
            rl.blurMatrices.clear();
            if (rgbDelay > 0.0f) {
                const f64 d = std::clamp(static_cast<f64>(rgbDelay), 0.0, 60.0);
                rl.temporal.push_back({world_2d_frac(comp, *l, t0), 1.0f, Vec3{1, 0, 0}, t0});
                rl.temporal.push_back({world_2d_frac(comp, *l, t0 - d), 1.0f, Vec3{0, 1, 0}, t0 - d});
                rl.temporal.push_back({world_2d_frac(comp, *l, t0 - 2.0 * d), 1.0f, Vec3{0, 0, 1}, t0 - 2.0 * d});
            } else {
                rl.temporal.push_back({m, 1.0f, Vec3{0, 0, 0}, t0});
            }
            if (shifted && rgbDelay > 0.0f) for (auto& t : rl.temporal) t.m = t.m * shiftM;
            if (echoCount > 0) {
                const u32 n = std::min<u32>(echoCount, 16u);
                const f64 d = std::clamp(static_cast<f64>(echoDelay), 0.25, 120.0);
                f32 w = 1.0f;
                for (u32 i = 1; i <= n; ++i) {
                    w *= std::clamp(echoDecay, 0.0f, 1.0f);
                    if (w < 0.01f) break;
                    rl.temporal.push_back({world_2d_frac(comp, *l, t0 - static_cast<f64>(i) * d) * shiftM, w, Vec3{0, 0, 0},
                                           t0 - static_cast<f64>(i) * d});
                }
            }
        }

        // RGB NO TEMPO (Fase 7.3 §25): cada canal de cor vem de um INSTANTE
        // diferente da camada. Aqui só se leem os parâmetros; as fontes extras
        // são buscadas no bloco de vídeo, que é quem fala com o decoder.
        //
        // Com o Eco ligado junto, o RGB no tempo MANDA: os dois somam imagem
        // inteira por caminhos diferentes e o resultado estouraria. Trocar é
        // mais honesto que somar e mentir sobre a intensidade.
        bool rgbOn = false;
        f32 rgbOffsets[3] = {0.0f, 0.0f, 0.0f};
        f32 rgbUnit = 1.0f;
        f32 rgbAmount = 0.0f;
        if (effects_ && !wants_3d(comp, *l, time)) {
            const EffectTypeId rgbType = rgbTimeType_;
            const ParameterRegistry* rp = rgbType ? effects_->params(rgbType) : nullptr;
            for (const EffectInstance& inst : l->effects) {
                if (!inst.enabled || inst.type != rgbType || !rp || rp->count() < 6) continue;
                rgbOffsets[0] = evaluate_param(l->tracks, inst, 0, rp->at(0), local).v[0];
                rgbOffsets[1] = evaluate_param(l->tracks, inst, 1, rp->at(1), local).v[0];
                rgbOffsets[2] = evaluate_param(l->tracks, inst, 2, rp->at(2), local).v[0];
                const bool seconds = evaluate_param(l->tracks, inst, 3, rp->at(3), local).as_enum() == 1;
                rgbAmount = std::clamp(evaluate_param(l->tracks, inst, 4, rp->at(4), local).v[0] / 100.0f, 0.0f, 1.0f);
                rgbUnit = seconds ? static_cast<f32>(comp.fps()) : 1.0f;
                rgbOn = rgbAmount >= 0.01f;
                break;   // um RGB no tempo por camada
            }
        }
        if (rgbOn) {
            // O deslocamento por canal já está aplicado nas fontes extras; a
            // camada não precisa de amostras de matriz.
            rl.temporal.clear();
        }
        // TIMESLICE: N faixas, cada uma num instante. Só vídeo e pré-composição
        // têm conteúdo que muda no tempo; o desenho recorta cada amostra na
        // faixa dela (composite/time_slice.frag). Manda sobre Eco e RGB no tempo.
        if (effects_ && timeSliceType_ && !wants_3d(comp, *l, time)
            && (rl.source.kind == LayerSource::Kind::Video || rl.source.kind == LayerSource::Kind::Nested)) {
            const ParameterRegistry* sp = effects_->params(timeSliceType_);
            for (const EffectInstance& inst : l->effects) {
                if (!inst.enabled || inst.type != timeSliceType_ || !sp || sp->count() < 4) continue;
                const f32 dir = evaluate_param(l->tracks, inst, 0, sp->at(0), local).v[0];
                const i32 n = std::clamp(evaluate_param(l->tracks, inst, 1, sp->at(1), local).as_int(), 1, 32);
                const f32 off = evaluate_param(l->tracks, inst, 2, sp->at(2), local).v[0];
                const bool interp = evaluate_param(l->tracks, inst, 3, sp->at(3), local).as_bool();
                const f32 rad = (std::isfinite(dir) ? dir : 90.0f) * kDeg2Rad;
                const f64 base = static_cast<f64>(time.value) + (std::isfinite(off) ? static_cast<f64>(off) : 0.0);
                rl.temporal.clear();
                rl.blurMatrices.clear();
                rgbOn = false;
                for (i32 s = 0; s < n; ++s) {
                    const f64 rel = n > 1 ? -0.5 * n + static_cast<f64>(s) * n / (n - 1) : 0.0;
                    const f64 at = base + rel;
                    const Vec4 slice{std::cos(rad), std::sin(rad), static_cast<f32>(s), static_cast<f32>(n)};
                    const f64 lo = std::floor(at + 1e-6), frac = at - lo;
                    if (interp && frac > 1e-3 && frac < 1.0 - 1e-3) {
                        RenderLayer::TemporalSample a{m, static_cast<f32>(1.0 - frac), Vec3{0, 0, 0}, lo};
                        RenderLayer::TemporalSample b{m, static_cast<f32>(frac), Vec3{0, 0, 0}, lo + 1.0};
                        a.slice = slice; b.slice = slice;
                        rl.temporal.push_back(std::move(a));
                        rl.temporal.push_back(std::move(b));
                    } else {
                        RenderLayer::TemporalSample a{m, 1.0f, Vec3{0, 0, 0}, std::floor(at + 0.5)};
                        a.slice = slice;
                        rl.temporal.push_back(std::move(a));
                    }
                }
                break;   // um TimeSlice por camada
            }
        }
        // TEMPO AMPLO (CC Wide Time, 0.3.0): média de pesos iguais das amostras
        // de t − trás a t + frente. Vídeo e pré-composição trazem o CONTEÚDO de
        // cada instante (`content`: o decoder/a filha, como as faixas do
        // TimeSlice); com "Incluir movimento" cada amostra também vai para o
        // transform daquele instante. "Desfoque nativo" preenche o intervalo
        // entre passos com subamostras: no conteúdo, os dois quadros vizinhos
        // misturados pela fração. Manda sobre Eco, RGB no tempo e o desfoque.
        if (effects_ && wideTimeType_ && !wants_3d(comp, *l, time)) {
            const ParameterRegistry* wp = effects_->params(wideTimeType_);
            for (const EffectInstance& inst : l->effects) {
                if (!inst.enabled || inst.type != wideTimeType_ || !wp || wp->count() < 4) continue;
                const i32 fwd = std::clamp(evaluate_param(l->tracks, inst, 0, wp->at(0), local).as_int(), 0, 30);
                const i32 back = std::clamp(evaluate_param(l->tracks, inst, 1, wp->at(1), local).as_int(), 0, 30);
                const bool native = evaluate_param(l->tracks, inst, 2, wp->at(2), local).as_bool();
                const bool motion = evaluate_param(l->tracks, inst, 3, wp->at(3), local).as_bool();
                const bool content = rl.source.kind == LayerSource::Kind::Video || rl.source.kind == LayerSource::Kind::Nested;
                if (fwd + back == 0 || (!content && !motion)) break;
                const u32 span = static_cast<u32>(fwd + back);
                const u32 sub = native ? std::clamp<u32>(comp.motion_blur().samples / std::max<u32>(1u, span / 4u + 1u), 2u, 8u) : 1u;
                const u32 total = span * sub + 1u;
                const f32 w = 1.0f / static_cast<f32>(total);
                const f64 t0 = static_cast<f64>(time.value);
                rl.temporal.clear();
                rl.blurMatrices.clear();
                rgbOn = false;
                for (u32 j = 0; j < total; ++j) {
                    const f64 ts = t0 - static_cast<f64>(back) + static_cast<f64>(j) / static_cast<f64>(sub);
                    const bool now = std::fabs(ts - t0) < 1e-9;
                    const Mat4 sm = (!motion || now) ? m : world_2d_frac(comp, *l, ts) * shiftM;
                    if (!content) { rl.temporal.push_back({sm, w, Vec3{0, 0, 0}, ts}); continue; }
                    const f64 lo = std::floor(ts + 1e-6), frac = ts - lo;
                    RenderLayer::TemporalSample a{sm, w * static_cast<f32>(frac > 1e-3 ? 1.0 - frac : 1.0), Vec3{0, 0, 0}, lo};
                    a.content = true;
                    rl.temporal.push_back(std::move(a));
                    if (frac > 1e-3) {
                        RenderLayer::TemporalSample b{sm, w * static_cast<f32>(frac), Vec3{0, 0, 0}, lo + 1.0};
                        b.content = true;
                        rl.temporal.push_back(std::move(b));
                    }
                }
                break;   // um Tempo amplo por camada
            }
        }
        // DETECTAR MOVIMENTO: o atraso (quadros) do primeiro efeito ligado. A
        // fonte nesse instante é buscada no bloco de vídeo (o decoder); a
        // comparação acontece na cadeia de efeitos (EffectGraph::build).
        i64 motionDelay = 0;
        if (effects_
            && (l->kind == LayerKind::Video || rl.source.kind == LayerSource::Kind::Nested)) {
            const ParameterRegistry* mp = effects_->params(motionDetectType_);
            const ParameterRegistry* dp = datamoshType_ ? effects_->params(datamoshType_) : nullptr;
            const u32 holdIndex = dp ? dp->find("hold_frames") : kInvalidIndex;
            for (const EffectInstance& inst : l->effects) {
                // DATAMOSH: a referência fica presa por Q quadros e renova — no
                // quadro local n, a fonte em n − ((n mod Q) + 1). Um quadro de
                // histórico por camada: vale o primeiro efeito que pede.
                if (inst.enabled && datamoshType_ && inst.type == datamoshType_ && dp && holdIndex < dp->count()) {
                    const f32 q = evaluate_param(l->tracks, inst, holdIndex, dp->at(holdIndex), local).v[0];
                    const i64 hold = std::isfinite(q) ? std::clamp<i64>(std::llround(q), 1, kDatamoshMaxHold) : 12;
                    motionDelay = ((local.value % hold) + hold) % hold + 1;
                    break;
                }
                const Effect* temporal=inst.enabled ? effects_->find(inst.type) : nullptr;
                if (temporal && temporal->wants_history() && inst.type!=motionDetectType_ && inst.type!=datamoshType_) {
                    const ParameterRegistry* specs=effects_->params(inst.type);
                    if (specs) {
                        std::vector<ParamValue> values;
                        values.reserve(specs->count());
                        for (u32 p=0;p<specs->count();++p) values.push_back(evaluate_param(l->tracks,inst,p,specs->at(p),local));
                        EffectEval evaluation;
                        evaluation.effect=temporal;evaluation.instance=&inst;evaluation.layer=l;
                        evaluation.values=values.data();evaluation.count=specs->count();
                        evaluation.localTime=local;evaluation.framesPerSecond=fps;
                        const f64 delay=temporal->history_delay_frames(evaluation);
                        if (std::isfinite(delay) && delay>=1.0) {
                            motionDelay=std::clamp<i64>(static_cast<i64>(std::floor(delay)),1,1200);
                            break;
                        }
                    }
                }
                if (!inst.enabled || inst.type != motionDetectType_ || !mp || mp->count() < 1) continue;
                const f32 d = evaluate_param(l->tracks, inst, 0, mp->at(0), local).v[0];
                motionDelay = std::isfinite(d) ? std::clamp<i64>(std::llround(d), 1, kMotionDetectMaxDelay) : 1;
                break;
            }
        }

        // APRIMORAR (temporal, 0.3.0): os vizinhos no tempo da FONTE que o
        // efeito pede (Effect::temporal_neighbors). Só vídeo e pré-composição
        // têm outro quadro; foto, texto e forma ficam no espacial. O vídeo
        // busca os quadros no bloco do decoder; a pré-composição, logo abaixo.
        rl.neighbors.clear();
        if (effects_ && (rl.source.kind == LayerSource::Kind::Video || rl.source.kind == LayerSource::Kind::Nested)) {
            std::vector<i32> offsets;
            temporal_neighbor_offsets(*l, *effects_, local, fps, offsets);
            for (const i32 off : offsets) {
                RenderLayer::NeighborSample nb;
                nb.offset = off;
                rl.neighbors.push_back(std::move(nb));
            }
        }

        // PRÉ-COMPOSIÇÃO COM EFEITO DE TEMPO (beta 2140): o vídeo busca o
        // passado no decoder; a pré-composição não tem decoder — o passado
        // dela é a FILHA renderizada naquele instante. Sem isto, o eco somava
        // N cópias do quadro de agora (estourava), o Detectar movimento
        // comparava o quadro com ele mesmo (preto) e o RGB no tempo juntava
        // três canais iguais (nada). Cada instante extra é uma filha a mais em
        // `out.nested`, com o mesmo mapeamento de tempo da principal (entrada,
        // velocidade, reverso, remapeamento) e um espaço de ids próprio por
        // vaga: decoders e caches da filha principal não são puxados para trás.
        if (rl.source.kind == LayerSource::Kind::Nested && prepareDepth_ < 8
            && (rgbOn || motionDelay > 0 || !rl.temporal.empty() || !rl.neighbors.empty())) {
            const Composition* child = project.timeline().composition(l->nested.composition);
            if (child && child != &comp) {
                const f64 childFps = child->fps() > 0.0 ? child->fps() : fps;
                const i64 lastChild = std::max<i64>(0, child->duration().value - 1);
                // Mesmo deslocamento da camada (Posterizar tempo age em layerTime).
                const f64 layerShift = static_cast<f64>(layerTime.value - time.value);
                auto childFrame = [&](f64 compTime) {
                    const i64 t = static_cast<i64>(std::floor(compTime + layerShift + 1e-6));
                    const f64 f = l->source_frame(FrameIndex{t}) * childFps / fps;
                    return std::clamp<i64>(std::isfinite(f) ? static_cast<i64>(std::floor(f + 1e-6)) : 0, 0, lastChild);
                };
                const i64 mainCf = std::clamp<i64>(static_cast<i64>(std::floor(l->source_frame(layerTime) * childFps / fps + 1e-6)), 0, lastChild);
                std::vector<std::pair<i64, u32>> made;   // quadro da filha → índice
                u32 slot = 0;
                auto nestedAtFrame = [&](i64 cf) -> u32 {
                    if (cf == mainCf) return rl.source.nestedIndex;
                    for (const auto& [frame, index] : made) if (frame == cf) return index;
                    auto snap = std::make_unique<FrameSnapshot>();
                    const u32 savedSalt = nestSalt_;
                    const usize savedPathSize = renderInstancePath_.size();
                    renderInstancePath_.push_back(id.pack());
                    renderInstancePath_.push_back(l->nested.composition.pack());
                    renderInstancePath_.push_back(0x7E000000ull + slot++);   // vaga temporal
                    auto ns = nestedNamespaces_.find(renderInstancePath_);
                    if (ns == nestedNamespaces_.end()) {
                        if (nestedNamespaces_.size() >= 0x3FFFu) {
                            renderInstancePath_.resize(savedPathSize);
                            mark_incomplete(__FILE__, __LINE__);
                            return ~0u;
                        }
                        const u32 next = static_cast<u32>(nestedNamespaces_.size()) + 1u;
                        ns = nestedNamespaces_.emplace(renderInstancePath_, next).first;
                    }
                    nestSalt_ = ns->second;
                    ++prepareDepth_;
                    RenderSettings childSettings = settings;
                    childSettings.sceneEditor.enabled = false;
                    const CameraPassThrough savedThrough = g_cameraPassThrough;
                    g_cameraPassThrough = CameraPassThrough{};
                    prepare(*child, project, FrameIndex{cf}, media, imageLookup, imageCtx, childSettings, frameNumber,
                            playDirection, decodeMode, speed, *snap);
                    g_cameraPassThrough = savedThrough;
                    --prepareDepth_;
                    nestSalt_ = savedSalt;
                    renderInstancePath_.resize(savedPathSize);
                    frameNumber_ = frameNumber;
                    out.videoLayers += snap->videoLayers;
                    out.staleVideoFrames += snap->staleVideoFrames;
                    out.missingVideoFrames += snap->missingVideoFrames;
                    out.videoSourceWaits.insert(out.videoSourceWaits.end(),
                        snap->videoSourceWaits.begin(), snap->videoSourceWaits.end());
                    const u32 index = static_cast<u32>(out.nested.size());
                    out.nested.push_back(std::move(snap));
                    made.emplace_back(cf, index);
                    return index;
                };
                auto nestedAt = [&](f64 compTime) -> u32 { return nestedAtFrame(childFrame(compTime)); };
                if (rgbOn) {
                    for (u32 c = 0; c < 3; ++c)
                        rl.source.nestedChannel[c] = nestedAt(static_cast<f64>(time.value)
                                                              + static_cast<f64>(rgbOffsets[c] * rgbUnit * rgbAmount));
                }
                if (motionDelay > 0) rl.source.nestedHistory = nestedAt(static_cast<f64>(time.value - motionDelay));
                for (RenderLayer::TemporalSample& sample : rl.temporal) sample.nested = nestedAt(sample.time);
                // Aprimorar: os vizinhos são quadros da FILHA (±k na grade dela).
                // Na ponta da filha o vizinho cairia no próprio quadro: sai.
                std::vector<RenderLayer::NeighborSample> kept;
                for (RenderLayer::NeighborSample& nb : rl.neighbors) {
                    const i64 cf = mainCf + nb.offset;
                    if (cf < 0 || cf > lastChild) continue;
                    nb.nested = nestedAtFrame(cf);
                    if (nb.nested != ~0u && nb.nested != rl.source.nestedIndex) kept.push_back(std::move(nb));
                }
                rl.neighbors = std::move(kept);
            } else {
                rl.neighbors.clear();
            }
        } else if (rl.source.kind == LayerSource::Kind::Nested) {
            rl.neighbors.clear();
        }

        // Partículas (8.2, ParticleScene.cpp): cena 3D, espaço mundo, emissão no
        // nascimento e desfoque por tempo. No espaço da composição a textura
        // já sai no lugar (composição ← camada = identidade).
        if (rl.source.kind == LayerSource::Kind::Particles) {
            prepare_particle_space(comp, *l, local, settings, inScene3d, rl, out);
            if (rl.particle.compSpace) {
                m = Mat4::identity();
                rl.compFromLayer = m;
                rl.texelScale = texel_scale_for(previewFactor);
            }
        }

        // Plano de efeitos ANTES do decode: é ele que diz se a camada pode
        // alcançar a tela (Fase 8C §19–22).
        if (out.plans.size() <= used) out.plans.emplace_back();
        {
            LayerPlacement placement;
            placement.compFromLayer = m;
            placement.compWidth = out.compWidth;
            placement.compHeight = out.compHeight;
            placement.layerWidth = rl.source.width;
            placement.layerHeight = rl.source.height;
            // Vai para a cena 3D (plano no mundo): o corte pela área visível da
            // composição não se aplica — ver LayerPlacement::inScene3d.
            placement.inScene3d = inScene3d;
            if (effectCameraActive) {
                placement.cameraActive = true;
                placement.cameraView = cam3d.view;
                placement.cameraFovY = cam3d.fovY;
            }
            if (particularSheet || cameraEmitter) {
                placement.particleSpace = true;
                placement.worldFromLayer = particularSheet ? particularWorld : world_3d(comp, *l, time) * shiftM;
                placement.compFromWorld = compFromClip * viewProj3d;
                placement.particleLayerFromComp = particleLayerFromComp;
                const f64 previousTime = static_cast<f64>(time.value) - 1.0;
                const Mat4 previousCamera = settings.sceneEditor.enabled && !settings.finalQuality
                    ? compFromClip * viewProj3d
                    : comp_view_projection_frac(comp, previousTime, out.compWidth, out.compHeight);
                placement.previousParticleProjection = previousCamera * world_3d_frac(comp, *l, previousTime);
                if (!particularSheet) placement.previousParticleProjection = placement.previousParticleProjection * shiftM;
                // Linhas da vista = eixos da câmera no mundo.
                const Mat4& v = cam3d.view;
                placement.camRight = Vec3{v.col[0].x, v.col[1].x, v.col[2].x};
                placement.camUp = Vec3{v.col[0].y, v.col[1].y, v.col[2].y};
                if (particularSheet) {
                    rl.blurMatrices.clear();   // a folha não se move: o movimento é das partículas
                    rl.temporal.clear();
                }
                if (particularSheet && l->motionBlur && comp.motion_blur().enabled) {
                    placement.shutterAngle = comp.motion_blur().shutterAngle
                                           * motion_blur_amount(l->transform.motionBlurAmount);
                    placement.shutterPhase = comp.motion_blur().shutterPhase
                                           * motion_blur_amount(l->transform.motionBlurAmount);
                }
            }
            EffectGraph::plan(*l, *effects_, local, rl.texelScale, placement, this, out.plans[used], fps, &comp);
            // Integrate the actual parameter curves; interpolating matrices
            // shrinks rotating objects and incorrectly anticipates Hold keys.
            const u32 foldIndex = out.plans[used].foldEffectIndex;
            if (layerShutter.duration > 0 && foldIndex < l->effects.size()
                && !inScene3d && !particularSheet && rl.temporal.empty() && !rgbOn) {
                const EffectInstance& instance = l->effects[foldIndex];
                const Effect* effect = effects_->find(instance.type);
                const ParameterRegistry* params = effects_->params(instance.type);
                std::vector<ParamValue> values(params ? params->count() : 0);
                auto matrixAt = [&](f64 offset) {
                    Mat4 fold = Mat4::identity();
                    if (effect && params) {
                        for (u32 p = 0; p < params->count(); ++p)
                            values[p] = evaluate_param_f(l->tracks, instance, p, params->at(p), local.value + offset);
                        EffectEval eval;
                        eval.effect = effect; eval.instance = &instance; eval.layer = l;
                        eval.values = values.data(); eval.count = static_cast<u32>(values.size());
                        eval.fractionalTime = local.value + offset; eval.framesPerSecond = fps;
                        eval.localTime = FrameIndex{static_cast<i64>(std::floor(eval.fractionalTime))};
                        eval.placement = &placement; eval.texelScale = rl.texelScale;
                        effect->resolve_resources(eval); // e.g. integrated oscillator phase at this sample
                        f32 opacity = 1.f;
                        (void)effect->fold_into_composite(eval, fold, opacity);
                    }
                    return shutterMatrix(offset) * fold;
                };
                const f64 travel = shutter_projected_path(layerShutter, static_cast<f32>(rl.source.width),
                    static_cast<f32>(rl.source.height), matrixAt, previewFactor);
                layerShutter.count = rl.source.glyphSets > 1 ? rl.source.glyphSets
                    : capped_shutter_samples(comp.motion_blur(), settings.finalQuality,
                        effective_quality(settings).motionBlurSamples, travel);
                if (layerShutter.count > 1) {
                    rl.blurMatrices.resize(layerShutter.count);
                    for (u32 si = 0; si < layerShutter.count; ++si) rl.blurMatrices[si] = matrixAt(layerShutter.offset(si));
                    rl.blurIncludesFold = true;
                } else rl.blurMatrices.clear();
            }
        }
        if (!inScene3d) {
            // 2D composition has no depth. A domain effect can carry Z (for
            // its 3D orbit mode); keep XY and homogeneous W but do not let
            // that residual Z clip the entire 2D layer outside the GPU range.
            auto flatten = [](Mat4& matrix) { for (auto& column : matrix.col) column.z = 0.f; };
            flatten(rl.compFromLayer);
            for (auto& matrix : rl.blurMatrices) flatten(matrix);
            for (auto& sample : rl.temporal) flatten(sample.m);
            if (out.plans[used].hasFold) flatten(out.plans[used].foldMatrix);
        }
        // FORA DA TELA: a caixa da camada (com o Transform dobrado) não toca a
        // composição e nada na pilha dela pode trazer pixel para dentro (só
        // efeitos de cor, sem desfoque/eco/RGB no tempo, sem perspectiva) —
        // então ela não decodifica, não sobe textura e não gera passe. Vale
        // também para matte: uma matte fora da tela recorta exatamente como
        // uma matte ausente (nada no normal, tudo no invertido).
        if (!inScene3d && rl.blurMatrices.empty() && rl.temporal.empty() && !rgbOn
            && rl.source.width > 0 && rl.source.height > 0) {
            const EffectPlan& plan = out.plans[used];
            bool colorOnly = true;
            for (const EffectStage& st : plan.stages) colorOnly &= st.kind == EffectStage::Kind::FusedColor;
            if (colorOnly) {
                const Mat4 mm = plan.hasFold ? m * plan.foldMatrix : m;
                const f32 lw = static_cast<f32>(rl.source.width), lh = static_cast<f32>(rl.source.height);
                f32 x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
                bool affine = true;
                for (const Vec2 c : {Vec2{0, 0}, Vec2{lw, 0}, Vec2{0, lh}, Vec2{lw, lh}}) {
                    const Vec4 q = mm * Vec4{c.x, c.y, 0, 1};
                    affine &= std::fabs(q.w - 1.0f) < 1e-4f;
                    x0 = std::min(x0, q.x); x1 = std::max(x1, q.x);
                    y0 = std::min(y0, q.y); y1 = std::max(y1, q.y);
                }
                // 2 px de folga: o filtro bilinear da borda.
                const f32 cw = static_cast<f32>(out.compWidth), chh = static_cast<f32>(out.compHeight);
                if (affine && (x1 < -2.0f || y1 < -2.0f || x0 > cw + 2.0f || y0 > chh + 2.0f)) {
                    if (rl.maskCount > 0) out.maskData.resize(rl.maskFirst);
                    ++out.culledLayers;
                    continue;
                }
            }
        }

        // Vídeo: pede o frame e pega o melhor disponível AGORA (nunca espera).
        if (rl.source.kind == LayerSource::Kind::Video && media) {
            const Asset* asset = project.asset(l->source);
            VideoSource* src = media->source_for(rid, l->source, *asset, frameNumber, settings.finalQuality);
            const u32 missingBefore = out.missingVideoFrames;
            const u32 staleBefore = out.staleVideoFrames;
            if (src) {
                src->set_epoch(settings.mediaGeneration);
                const VideoStreamInfo streamInfo = src->info();
                // Flow Retime / Flow Motion Blur (0.1.8): o instante da fonte, o
                // modo dos quadros e o desfoque vetorial vêm dos efeitos, se houver.
                const VideoFlowFx flowFx = video_flow_fx(*l, static_cast<f64>(layerTime.value),
                                                         static_cast<f64>(comp.motion_blur().shutterAngle) / 360.0);
                const f64 srcFrameF = l->source_frame_f(flowFx.srcTimeline);
                u8 frameBlend = flowFx.frameBlend;
                i64 mediaUs = static_cast<i64>(std::llround(srcFrameF * 1e6 / fps));
                // Mistura de quadros: posição FRACIONÁRIA na grade da fonte.
                i64 nextUs = -1;
                f32 blendT = 0.0f;
                f64 shutterTravel = 0.0;
                if (flowFx.vectorAmount > 0.0f) {
                    if (flowFx.retime) {
                        // Retime: o obturador cobre o tempo da fonte que o retime anda.
                        const f64 h = flowFx.shutter * 0.5, t0 = static_cast<f64>(layerTime.value);
                        const f64 before = l->source_frame_f(flow_retime_timeline(*l, *flowFx.retime, t0 - h));
                        const f64 after = l->source_frame_f(flow_retime_timeline(*l, *flowFx.retime, t0 + h));
                        shutterTravel = std::fabs(srcFrameF - before) + std::fabs(after - srcFrameF);
                    } else {
                        shutterTravel = l->source_shutter_travel(static_cast<f64>(layerTime.value), flowFx.shutter);
                    }
                }
                const bool wantVector = flowFx.vectorAmount > 0.0f && shutterTravel > 1e-8;
                if (streamInfo.preciseFrameTiming && (frameBlend >= 1 || wantVector)) {
                    bool currentReady = false;
                    const FrameRef current = src->frame_for(mediaUs, &currentReady);
                    if (currentReady && current && current->durationUs > 0) {
                        const i64 next = current->ptsUs + current->durationUs;
                        const f64 fraction = static_cast<f64>(mediaUs - current->ptsUs) / current->durationUs;
                        const bool between = frameBlend >= 1 && fraction > 0.01 && fraction < 0.99;
                        if ((streamInfo.durationUs <= 0 || next < streamInfo.durationUs) && (between || wantVector)) {
                            nextUs = next;
                            blendT = between ? static_cast<f32>(fraction) : 0.0f;
                        }
                    }
                } else if (frameBlend >= 1 || wantVector) {
                    if (const f64 srcFps = streamInfo.fps; srcFps > 0.0) {
                        const f64 pos = srcFrameF / fps * srcFps;
                        const f64 idx = std::floor(pos + 1e-3);
                        const f64 frac = pos - idx;
                        const i64 us = static_cast<i64>(std::llround((idx + 1.0) * 1e6 / srcFps));
                        const bool inside = streamInfo.durationUs <= 0 || us < streamInfo.durationUs;
                        const bool between = frameBlend >= 1 && frac > 0.01 && frac < 0.99;
                        if (inside && (between || wantVector)) {
                            nextUs = us;
                            blendT = between ? static_cast<f32>(frac) : 0.0f;
                        }
                    }
                }
                // Na grade de quadros da FONTE: o quadro que está na tela no
                // instante t (piso), não o "mais próximo". Composição a 60 sobre
                // vídeo a 30 cai a cada dois quadros exatamente entre dois
                // quadros da fonte — a meia distância de ambos, nenhum era
                // "exato", e o export esperava o decoder até estourar o prazo.
                if (const f64 srcFps = streamInfo.fps; srcFps > 0.0 && !streamInfo.preciseFrameTiming) {
                    const f64 idx = std::floor(static_cast<f64>(mediaUs) * srcFps / 1e6 + 1e-3);
                    mediaUs = static_cast<i64>(std::llround(idx * 1e6 / srcFps));
                }
                const i64 dur = streamInfo.durationUs;
                if (dur > 0) mediaUs = std::clamp<i64>(mediaUs, 0, last_source_timestamp(streamInfo,src->frame_duration_us()));
                // FLOW RETIME, "Substituir quadros duplicados" (0.3.0): o quadro da
                // fonte que REPETE o anterior (30 dentro de 60, quadro perdido ou
                // congelado) é refeito pelo optical flow entre os quadros
                // DISTINTOS em volta — P, o começo da série repetida, e N, o
                // primeiro diferente depois dela; na posição fracionária `pos` da
                // fonte, o instante entre os dois é t = (pos − P)/(N − P). A
                // decisão vem das assinaturas de luma (uma por quadro da fonte,
                // guardadas): a que falta faz a prévia mostrar o quadro como está
                // e o export esperar o quadro chegar.
                bool duplicateMissing = false;
                i64 duplicateWantUs = -1;
                if (flowFx.retime && flowFx.replaceDuplicates && streamInfo.fps > 0.0) {
                    const f64 srcFps = streamInfo.fps;
                    const i64 lastUs = dur > 0 ? last_source_timestamp(streamInfo, src->frame_duration_us())
                                               : std::numeric_limits<i64>::max() / 4;
                    const i64 lastIndex = static_cast<i64>(std::floor(static_cast<f64>(lastUs) * srcFps / 1e6 + 1e-3));
                    const f64 pos = std::clamp(srcFrameF / fps * srcFps, 0.0, static_cast<f64>(lastIndex));
                    const i64 base = static_cast<i64>(std::floor(pos + 1e-3));
                    auto usOf = [&](i64 i) { return static_cast<i64>(std::llround(static_cast<f64>(i) * 1e6 / srcFps)); };
                    const u64 assetKey = l->source.pack();
                    // A assinatura do quadro i; `pending` = o quadro ainda não chegou.
                    // Quadro presente sem pixels legíveis na CPU: sem assinatura,
                    // e conta como distinto (nunca prende o export).
                    auto sigOf = [&](i64 i, bool& pending) -> const FrameSignature* {
                        const i64 us = usOf(i);
                        if (const FrameSignature* s = find_signature(assetKey, us)) return s;
                        bool ex = false;
                        const FrameRef f = src->frame_for(us, &ex);
                        if (f && ex) return frame_signature(assetKey, us, f.get());
                        if (duplicateWantUs < 0) duplicateWantUs = us;
                        duplicateMissing = true;
                        pending = true;
                        return nullptr;
                    };
                    // 1 = repete o anterior, 0 = distinto, −1 = ainda não se sabe.
                    auto dupAt = [&](i64 i) -> int {
                        if (i <= 0) return 0;
                        bool pending = false;
                        const FrameSignature* a = sigOf(i, pending);
                        const FrameSignature* b = sigOf(i - 1, pending);
                        if (pending) return -1;
                        if (!a || !b) return 0;
                        return signatures_match(*a, *b, flowFx.duplicateSensitivity) ? 1 : 0;
                    };
                    constexpr i64 kReach = 6;   // série repetida mais longa que se refaz
                    bool known = true;
                    i64 first = base;
                    for (i64 k = 0; k < kReach; ++k) {
                        const int d = dupAt(first);
                        if (d < 0) { known = false; break; }
                        if (d == 0) break;
                        --first;
                    }
                    i64 next = base + 1;
                    for (i64 k = 0; known && k < kReach && next <= lastIndex; ++k) {
                        const int d = dupAt(next);
                        if (d < 0) { known = false; break; }
                        if (d == 0) break;
                        ++next;
                    }
                    if (known && next <= lastIndex && first < next && !(first == base && next == base + 1)) {
                        const f64 t = (pos - static_cast<f64>(first)) / static_cast<f64>(next - first);
                        mediaUs = usOf(first);
                        if (t > 1e-3) {
                            nextUs = usOf(next);
                            blendT = static_cast<f32>(std::clamp(t, 0.0, 1.0));
                            frameBlend = frameBlend == 1 ? u8{1} : u8{2};   // a mistura, se pedida; senão o movimento
                        } else {
                            nextUs = -1;
                            blendT = 0.0f;
                        }
                    }
                }
                if (!settings.finalQuality) {
                    for (const auto& effect : out.plans[used].evals) {
                        if (effect.foregroundSourceTimeUs < 0) continue;
                        // Keep video and matte together while the AI worker catches
                        // up. An old mask over a newer moving subject cuts off limbs.
                        mediaUs = effect.foregroundSourceTimeUs;
                        nextUs = -1;
                        blendT = 0;
                        break;
                    }
                }
                DecodeRequest req;
                i64 requiredTimes[8] = {mediaUs};
                u32 requiredCount = 1;
                if (nextUs >= 0) requiredTimes[requiredCount++] = nextUs;
                // Com mistura: primeiro o quadro atual; com ele no cache, o
                // seguinte (os dois precisam estar lá ao mesmo tempo).
                bool haveA = false;
                if (nextUs >= 0) (void)src->frame_for(mediaUs, &haveA);
                req.targetUs = nextUs >= 0 && haveA ? nextUs : mediaUs;
                req.mode = decodeMode;
                // Clipe reverso ou congelado: o decoder não tem embalo para a frente.
                req.direction = (l->speed == 0.0f || l->timeRemapEnabled) ? 0 : (l->reversed ? -playDirection : playDirection);
                if (flowFx.retime && playDirection != 0) {
                    // Flow Retime: a direção é a do tempo da fonte no próximo quadro.
                    const f64 b = l->source_frame_f(flow_retime_timeline(*l, *flowFx.retime,
                                                                        static_cast<f64>(layerTime.value + playDirection)));
                    req.direction = b > srcFrameF + 1e-6 ? 1 : (b < srcFrameF - 1e-6 ? -1 : 0);
                } else if (l->timeRemapEnabled && playDirection != 0) {
                    // Time remap (§16): a direção do DECODER é a da curva no
                    // próximo quadro — subindo, prefetch à frente; descendo,
                    // janela para trás; parada, só o alvo.
                    const f64 a = l->source_frame(layerTime);
                    const f64 b = l->source_frame(FrameIndex{layerTime.value + playDirection});
                    req.direction = b > a + 1e-6 ? 1 : (b < a - 1e-6 ? -1 : 0);
                }
                req.speed = speed * std::max(0.0f, l->speed) * static_cast<f32>(flowFx.rate);
                // O pedido do quadro principal: quem pede um quadro EXTRA (vizinho
                // do Aprimorar, assinatura de duplicado) só pede se ninguém trocou.
                const i64 mainTargetUs = req.targetUs;
                const DecodeMode mainMode = req.mode;
                bool exact = false;
                rl.source.frame = src->frame_for(mediaUs, &exact,
                    decodeMode == DecodeMode::Playback && !settings.finalQuality ? req.direction : 0);
                // QUADRO DE OUTRO TRECHO (o fantasma do corte): sem o exato, a
                // fonte devolve o mais próximo que tem no cache — e na entrada
                // de um clipe isso pode ser o keyframe do seek ou o quadro antes
                // do ponto de entrada (no split, a imagem da PRIMEIRA metade),
                // ou o que sobrou de outro ponto do clipe (parado lá antes do
                // play). Ia para a tela por um instante e, depois do prazo, para
                // o arquivo exportado. Aproximado só vale se é um quadro deste
                // trecho e, logo depois da entrada, não está adiante do alvo.
                // Sem candidato a camada fica sem quadro: a prévia segura a
                // imagem inteira anterior (PreviewRefill) e o export espera.
                if (rl.source.frame && !exact) {
                    const bool moving = (decodeMode == DecodeMode::Playback || decodeMode == DecodeMode::Scrub)
                                     && req.direction != 0;
                    const i64 entryFrames = std::max<i64>(2, static_cast<i64>(std::llround(fps * 0.5)));
                    const bool entering = moving && layerTime.value - l->start.value < entryFrames;
                    if (!video_fallback_allowed(*l, *rl.source.frame.get(), mediaUs, fps, streamInfo,
                                                src->frame_duration_us(), entering ? req.direction : 0)) {
                        rl.source.frame.reset();
                    }
                }
                if (nextUs >= 0) {
                    bool exactB = false;
                    FrameRef b = src->frame_for(nextUs, &exactB);
                    if (exact && exactB) {
                        rl.source.frameB = std::move(b);
                        rl.source.blendT = blendT;
                        // Aparelho em estado crítico: o preview mistura em vez de
                        // calcular o movimento de pixels (o export mantém o modo).
                        rl.source.blendMode = (frameBlend == 2 && !settings.finalQuality && settings.flowPolicyScale <= 0.25f) ? 1 : frameBlend;
                        rl.source.flowBase = flowFx.flowBase;
                        // Flow spans this actual source pair, not one timeline
                        // frame. Normalize the remapped shutter by its PTS span
                        // so VFR, different frame rates, ramps and freezes agree.
                        const i64 pairUs = rl.source.frameB->ptsUs - rl.source.frame->ptsUs;
                        rl.source.vectorBlur = wantVector && pairUs > 0 && fps > 0.0
                            ? static_cast<f32>(std::clamp(flowFx.vectorAmount, 0.0f, 4.0f) * shutterTravel * 1e6 / (fps * pairUs))
                            : 0.0f;
                    } else {
                        exact = false;   // falta um dos dois: o export espera; o preview mostra o que tem
                    }
                }
                rl.source.frameExact = exact;

                // RGB NO TEMPO: as fontes extras da MESMA camada, uma por
                // canal. Aqui, e não no acúmulo do desfoque de movimento,
                // porque só aqui o decoder é consultado — e o cache de quadros
                // faz as três leituras custarem o mesmo que uma quando os
                // instantes são próximos, que é sempre o caso.
                if (rgbOn) {
                    const i64 dur2 = streamInfo.durationUs;
                    i64 chanUs[3] = {0, 0, 0};
                    for (u32 c = 0; c < 3; ++c) {
                        const f64 shifted = static_cast<f64>(time.value)
                                          + static_cast<f64>(rgbOffsets[c] * rgbUnit * rgbAmount);
                        f64 us = l->source_frame(FrameIndex{static_cast<i64>(std::llround(shifted))}) * 1e6 / fps;
                        if (const f64 srcFps = streamInfo.fps; srcFps > 0.0 && !streamInfo.preciseFrameTiming) {
                            us = std::llround(std::floor(us * srcFps / 1e6 + 1e-3) * 1e6 / srcFps);
                        }
                        if (dur2 > 0) us = std::clamp<f64>(us, 0.0, static_cast<f64>(last_source_timestamp(streamInfo,src->frame_duration_us())));
                        chanUs[c] = static_cast<i64>(std::llround(us));
                        requiredTimes[requiredCount++] = chanUs[c];
                    }
                    // O canal MAIS ANTIGO do próximo quadro (um quadro da fonte
                    // adiante) já passou pelo decoder; sem esta reserva o cache
                    // o despejava (o mais longe do foco, que está no canal mais
                    // novo) e TODO quadro do export voltava ao keyframe para
                    // buscá-lo: ~90x mais lento, e em aparelho com GOP longo o
                    // prazo de 4 s estourava ("quadros de video indisponiveis").
                    if (requiredCount < 6) {
                        const i64 oldest = std::min(chanUs[0], std::min(chanUs[1], chanUs[2]));
                        requiredTimes[requiredCount++] = oldest + src->frame_duration_us();
                    }
                    // One decoder has one active target. Submitting A/R/G/B
                    // every prepare cancelled earlier channels before delivery.
                    // Request the first missing sample and retain the completed
                    // ones; the next prepare advances to the next missing sample.
                    bool requestedMissing = !exact;
                    req.mode = DecodeMode::Still;
                    req.direction = 0;
                    req.retainPreroll = true;
                    for (u32 c = 0; c < 3; ++c) {
                        bool ex = false;
                        rl.source.channel[c].frame = src->frame_for(chanUs[c], &ex);
                        rl.source.channel[c].mask = kChannelMasks[c];
                        if (!ex && !requestedMissing) {
                            req.targetUs = chanUs[c];
                            requestedMissing = true;
                        }
                        // O quadro APROXIMADO serve para o preview (melhor um
                        // canal fora do lugar do que um buraco), mas o export
                        // ESPERA: um RGB no tempo que mostrasse o quadro atual
                        // no vermelho não seria o efeito, seria um bug.
                        if (!rl.source.channel[c].frame || !ex) ++out.missingVideoFrames;
                    }
                    rl.source.channelCount = 3;
                }
                // TIMESLICE em vídeo: o quadro da fonte de cada faixa. O decoder
                // segura poucos quadros; o que já virou textura (sliceCache_)
                // não é pedido de novo. Um pedido por prepare, como o RGB no
                // tempo: o próximo prepare avança para a próxima faixa que falta.
                //   frame + sourceUs >= 0 → quadro exato novo: converter e guardar
                //   frame + sourceUs <  0 → aproximado: só a prévia, não guarda
                //   sem frame             → já está no cache da GPU
                // TOCANDO, nenhuma faixa vira pedido: o pedido Still trocava o
                // alvo do decoder a cada quadro (a faixa mais antiga que faltava
                // fica ATRÁS do playhead: seek ao keyframe), o pedido do quadro
                // atual voltava no prepare seguinte, cortava o decode da faixa
                // (descartada no caminho) e o seguinte pedia de novo — o decoder
                // vivia de seek e a prévia congelava. Tocando, as faixas entram
                // quando passam pela janela do playback (as de trás já viraram
                // textura quando estavam à frente); a que ainda não chegou sai
                // aproximada. Parado, o render seguinte busca o resto.
                if (rl.per_sample_content()) {
                    bool requestedMissing = !exact || (settings.previewPlaying && !settings.finalQuality);
                    const u64 sliceKey = rl.id.pack();
                    for (RenderLayer::TemporalSample& sample : rl.temporal) {
                        f64 us = l->source_frame(FrameIndex{static_cast<i64>(std::llround(sample.time))}) * 1e6 / fps;
                        if (const f64 srcFps = streamInfo.fps; srcFps > 0.0 && !streamInfo.preciseFrameTiming)
                            us = std::llround(std::floor(us * srcFps / 1e6 + 1e-3) * 1e6 / srcFps);
                        us = std::max(us, 0.0);
                        if (streamInfo.durationUs > 0)
                            us = std::min(us, static_cast<f64>(last_source_timestamp(streamInfo, src->frame_duration_us())));
                        const i64 sliceUs = static_cast<i64>(std::llround(us));
                        sample.sourceUs = sliceUs;
                        sample.frame.reset();
                        if (has_slice_texture(sliceKey, sliceUs)) continue;
                        bool ex = false;
                        FrameRef f = src->frame_for(sliceUs, &ex);
                        if (f && ex) { sample.frame = std::move(f); continue; }
                        // Faixa que falta NÃO conta como quadro de vídeo faltando:
                        // o decoder não segura todas ao mesmo tempo, e o export só
                        // renderiza quando nada falta. O desenho marca o quadro
                        // como incompleto; o render guarda as faixas que já
                        // chegaram e o export tenta de novo até fechar todas.
                        // A prévia mostra a aproximada enquanto isso (parada, o
                        // quadro também fica incompleto: ver finish_render).
                        if (f && !settings.finalQuality) { sample.frame = std::move(f); sample.sourceUs = -1; }
                        if (!requestedMissing) {
                            req.targetUs = sliceUs;
                            req.mode = DecodeMode::Still;
                            req.direction = 0;
                            req.retainPreroll = true;
                            requestedMissing = true;
                            if (requiredCount < 8) requiredTimes[requiredCount++] = sliceUs;
                        }
                    }
                }
                // APRIMORAR: os vizinhos no tempo — quadros da FONTE ±k em volta
                // do que está na tela. O exato vira textura no MESMO cache das
                // faixas do TimeSlice (o que já virou textura não é pedido de
                // novo); um pedido ao decoder por prepare, só depois do quadro
                // atual, e nenhum tocando (o playback traz os da frente; os de
                // trás viraram textura quando estavam à frente). Vizinho que
                // falta: a prévia segue sem ele (parada, marcada incompleta) e o
                // export espera. Na grade da fonte (fps constante) o instante é
                // exato; com tempo por quadro (VFR) vale o quadro vizinho que o
                // cache tiver, sem espera — um alvo entre dois quadros nunca
                // ficaria "exato" e prenderia o export.
                if (!rl.neighbors.empty()) {
                    const bool grid = !streamInfo.preciseFrameTiming && streamInfo.fps > 0.0;
                    const i64 frameUs = src->frame_duration_us() > 0 ? src->frame_duration_us()
                        : static_cast<i64>(std::llround(1e6 / std::max(1.0, streamInfo.fps)));
                    const i64 lastUs = streamInfo.durationUs > 0 ? last_source_timestamp(streamInfo, src->frame_duration_us())
                                                                 : std::numeric_limits<i64>::max() / 4;
                    const i64 baseUs = rl.source.frame ? rl.source.frame->ptsUs : mediaUs;
                    const f64 baseIndex = grid ? std::floor(static_cast<f64>(baseUs) * streamInfo.fps / 1e6 + 0.5) : 0.0;
                    const u64 sliceKey = rl.id.pack() ^ kNeighborSliceSalt;
                    const bool mayAsk = settings.finalQuality || !settings.previewPlaying;
                    bool asked = req.targetUs != mainTargetUs || req.mode != mainMode;
                    std::vector<RenderLayer::NeighborSample> kept;
                    kept.reserve(rl.neighbors.size());
                    for (RenderLayer::NeighborSample& nb : rl.neighbors) {
                        const i64 us = grid ? static_cast<i64>(std::llround((baseIndex + nb.offset) * 1e6 / streamInfo.fps))
                                            : baseUs + static_cast<i64>(nb.offset) * frameUs;
                        if (us < 0 || us > lastUs || us == baseUs) continue;   // além da fonte: esse lado não tem vizinho
                        nb.sourceUs = us;
                        nb.frame.reset();
                        if (has_slice_texture(sliceKey, us)) { kept.push_back(std::move(nb)); continue; }
                        bool ex = false;
                        FrameRef f = src->frame_for(us, &ex);
                        if (f && ex) { nb.frame = std::move(f); kept.push_back(std::move(nb)); continue; }
                        if (!grid) {
                            // Tempo por quadro: o vizinho do lado certo que houver.
                            if (f && (f->ptsUs - baseUs) * nb.offset > 0) {
                                nb.sourceUs = f->ptsUs;
                                nb.frame = std::move(f);
                                kept.push_back(std::move(nb));
                            }
                            continue;
                        }
                        if (requiredCount < 8) requiredTimes[requiredCount++] = us;
                        if (mayAsk && exact && !asked) {
                            req.targetUs = us;
                            req.mode = DecodeMode::Still;
                            req.direction = 0;
                            req.retainPreroll = true;
                            asked = true;
                        }
                        if (settings.finalQuality) ++out.missingVideoFrames;
                        else if (!settings.previewPlaying) mark_incomplete(__FILE__, __LINE__);
                    }
                    rl.neighbors = std::move(kept);
                }
                // Substituir duplicados: a assinatura que falta (um pedido por prepare).
                if (duplicateMissing && duplicateWantUs >= 0) {
                    if (requiredCount < 8) requiredTimes[requiredCount++] = duplicateWantUs;
                    const bool asked = req.targetUs != mainTargetUs || req.mode != mainMode;
                    if ((settings.finalQuality || !settings.previewPlaying) && exact && !asked) {
                        req.targetUs = duplicateWantUs;
                        req.mode = DecodeMode::Still;
                        req.direction = 0;
                        req.retainPreroll = true;
                    }
                    if (settings.finalQuality) ++out.missingVideoFrames;
                    else if (!settings.previewPlaying) mark_incomplete(__FILE__, __LINE__);
                }
                // DETECTAR MOVIMENTO: a mesma fonte `motionDelay` quadros da
                // timeline ANTES (no começo do clipe, presa ao primeiro quadro:
                // sem passado, sem movimento). O cache guarda os dois juntos;
                // tocando, o quadro de trás acabou de passar pelo decoder.
                if (motionDelay > 0) {
                    f64 us = l->source_frame(FrameIndex{layerTime.value - motionDelay}) * 1e6 / fps;
                    if (const f64 srcFps = streamInfo.fps; srcFps > 0.0 && !streamInfo.preciseFrameTiming) {
                        us = std::llround(std::floor(us * srcFps / 1e6 + 1e-3) * 1e6 / srcFps);
                    }
                    us = std::max(us, 0.0);
                    if (streamInfo.durationUs > 0)
                        us = std::min(us, static_cast<f64>(last_source_timestamp(streamInfo, src->frame_duration_us())));
                    const i64 histUs = static_cast<i64>(std::llround(us));
                    if (requiredCount < 8) requiredTimes[requiredCount++] = histUs;
                    // O de trás do PRÓXIMO quadro (um quadro da fonte adiante):
                    // sem a reserva o cache o despejava e o export voltava ao
                    // keyframe a cada quadro (o mesmo cuidado do RGB no tempo).
                    if (requiredCount < 8) requiredTimes[requiredCount++] = histUs + src->frame_duration_us();
                    bool histExact = false;
                    rl.source.historyFrame = src->frame_for(histUs, &histExact);
                    // Preview tocando: não interrompe o decoder para buscar o
                    // passado (um quadro aproximado serve). Parado, no scrub e
                    // no export, o decoder vai buscá-lo — depois do atual.
                    if (!histExact && exact && rl.source.channelCount == 0
                        && (settings.finalQuality || decodeMode != DecodeMode::Playback)) {
                        req.targetUs = histUs;
                        req.mode = DecodeMode::Still;
                        req.direction = 0;
                        req.retainPreroll = true;
                    }
                    // O export (e o quadro parado) ESPERA o quadro exato:
                    // comparar com o quadro errado inventaria (ou apagaria)
                    // movimento. Tocando, o aproximado passa (sem engasgo).
                    if ((!rl.source.historyFrame || !histExact)
                        && (settings.finalQuality || decodeMode != DecodeMode::Playback)) ++out.missingVideoFrames;
                }
                src->cache().set_required_times(requiredTimes, requiredCount, src->frame_duration_us() / 2);
                src->request(req);
                if (out.playbackPtsUs < 0 && rl.source.frame) {
                    out.playbackStream = streamInfo;
                    out.playbackDecode = src->stats();
                    out.playbackTargetUs = mediaUs;
                    out.playbackPtsUs = rl.source.frame->ptsUs;
                    out.playbackDurationUs = rl.source.frame->durationUs;
                    out.playbackPixelFormat = static_cast<u32>(rl.source.frame->format);
                }
                if (!rl.source.frame) ++out.missingVideoFrames;
                else if (!exact) ++out.staleVideoFrames;
            }
            if (!src) ++out.missingVideoFrames;
            if (settings.finalQuality && (out.missingVideoFrames != missingBefore
                    || out.staleVideoFrames != staleBefore)) {
                const auto sourceStats = src ? src->stats() : VideoSource::Stats{};
                const auto startup = src ? MediaManager::SourceStartup::Ready : media->source_startup(rid, l->source);
                out.videoSourceWaits.push_back({rid, l->source,
                    sourceStats.framesDelivered + sourceStats.framesDiscarded,
                    startup == MediaManager::SourceStartup::Opening,
                    out.missingVideoFrames != missingBefore,
                    out.staleVideoFrames != staleBefore,
                    startup == MediaManager::SourceStartup::Failed});
            }
            if (!rl.source.frame) {
                // Ainda nada decodificado (primeiro frame a caminho): a layer
                // fica de fora deste frame; o callback da fonte acorda o render.
                continue;
            }
        }

        // Imagem nova: sobe uma vez (a cópia dos pixels só acontece aqui).
        if (rl.source.kind == LayerSource::Kind::Image && backend_ && l->kind == LayerKind::Image) {
            const u64 key = l->source.pack();
            auto it = images_.find(key);
            // Rig posado: `width/height` viram a CAIXA da pose (maior que a
            // foto); a textura é a foto, no tamanho dos pixels. Com a caixa, o
            // envio lia além do fim dos pixels e o app fechava ao dobrar um osso.
            const u32 imgW = rl.source.rigCount > 0 ? rl.source.rigImageW : rl.source.width;
            const u32 imgH = rl.source.rigCount > 0 ? rl.source.rigImageH : rl.source.height;
            // Mesmo id com outro tamanho = outro conteúdo (projeto trocado
            // sem liberar, asset reimportado): a textura velha não serve.
            if (it != images_.end()
                && (it->second.width != imgW || it->second.height != imgH)) {
                destroy_image_linear(it->second);
                backend_->destroy_texture(it->second.texture);
                images_.erase(it);
                it = images_.end();
            }
            if (it == images_.end()) {
                TextureDesc d;
                d.width = imgW;
                d.height = imgH;
                d.format = SurfaceFormat::RGBA8;
                d.sampled = true;
                d.transferDst = true;
                d.debugName = "imagem";
                const bool enough = rl.source.pixels->rgba.size() >= static_cast<usize>(imgW) * imgH * 4;
                auto tex = enough ? backend_->create_texture(d) : Result<TextureHandle>{Status{Errc::InvalidArgument}};
                if (tex.ok()) {
                    PendingUpload up;
                    up.texture = *tex;
                    up.bytesPerRow = imgW * 4;
                    up.data = rl.source.pixels->rgba;
                    uploads_.push_back(std::move(up));
                    images_[key] = ImageTexture{*tex, imgW, imgH, frameNumber};
                }
            } else {
                it->second.lastFrame = frameNumber;
            }
            rl.source.pixels = nullptr;   // não vale fora do lock
        }

        // Camada 2D no espaço 3D: entra na cena (profundidade de verdade com os
        // modelos e as outras camadas 3D vizinhas na pilha). Com eco ou modo de
        // mistura ela continua na composição, como antes. Com DESFOQUE ela fica
        // na cena: a cena vira K sub-quadros e o plano vai em cada um na matriz
        // do sub-quadro. Fora da cena as faces de um cubo de camadas perdiam a
        // profundidade e se desenhavam na ordem da pilha (as de trás por cima).
        // Partículas 3D entram no grupo como billboards (ParticleScene), não
        // como plano.
        const bool particles = rl.source.kind == LayerSource::Kind::Particles;
        const bool asPlane = inScene3d && rl.temporal.empty() && rl.blend == BlendMode::Normal
                          && (!particles || rl.particle.inScene) && !out.plans[used].hasFold
                          && rl.matteMode == MatteMode::None && !rl.matteOnly && !clipInvolved;
        if (asPlane) {
            if (!groupOpen || out.scenes.empty()) {
                out.scenes.emplace_back();
                RenderLayer g;
                g.id = rid;
                g.source.kind = LayerSource::Kind::Scene3D;
                g.source.sceneGroup = static_cast<u32>(out.scenes.size() - 1);
                g.source.width = out.compWidth;
                g.source.height = out.compHeight;
                g.compFromLayer = Mat4::identity();
                g.texelScale = previewFactor;
                // O plano de efeitos desta camada vai com ela (índice seguinte).
                EffectPlan planeFx = std::move(out.plans[used]);
                out.plans[used].clear();
                out.layers.push_back(std::move(g));
                ++used;
                if (out.plans.size() <= used) out.plans.emplace_back();
                out.plans[used] = std::move(planeFx);
                groupOpen = true;
                if (!rl.matteOnly) lastDrawOrder = i;
            }
            if (particles) {
                out.scenes.back().particleLayers.push_back(used);
            } else {
                rl.planeGroup = static_cast<i32>(out.scenes.size() - 1);
                out.plans[used].placement.scenePlane = true;
                // O mundo da camada (o mesmo de `m`, sem a projeção): as luzes
                // da composição iluminam o plano pela normal dele.
                rl.worldFromLayer = shifted ? world_3d(comp, *l, time) * shiftM : world_3d(comp, *l, time);
                rl.acceptsLights = l->acceptsLights;
                out.scenes.back().planeLayers.push_back(used);
            }
        } else {
            groupOpen = false;
            if (!rl.matteOnly && !rl.particle.inScene) lastDrawOrder = i;
        }
        out.layers.push_back(std::move(rl));
        ++used;
    }
    // PRÉ-ROLAGEM DO CORTE: tocando, a camada de vídeo que entra logo adiante
    // (o pedaço seguinte de um split, o próximo clipe) já abre o decoder e
    // decodifica o 1º quadro dela ANTES do corte. Sem isto o decoder só abria
    // no quadro do corte (abrir + seek + decode) e o preview mostrava a
    // composição sem a camada: o preto que piscava ao passar por um split. O
    // export não passa aqui: ele espera o quadro exato de cada camada.
    if (media && !settings.finalQuality && playDirection > 0 && decodeMode == DecodeMode::Playback
        && stackLimit == ~0u) {   // o fundo de um ajuste noutro instante não pré-rola
        const i64 ahead = static_cast<i64>(std::ceil(fps * 1.5 * std::max(1.0f, speed)));
        for (u32 i = 0; i < n; ++i) {
            const LayerId id = order.at(i);
            const Layer* l = comp.layer(id);
            if (!l || l->kind != LayerKind::Video || !l->visible) continue;
            if (l->start.value <= time.value || l->start.value > time.value + ahead) continue;
            const Asset* asset = project.asset(l->source);
            if (!asset) continue;
            VideoSource* src = media->source_for(render_id(id), l->source, *asset, frameNumber, false);
            if (!src) continue;   // abrindo: o callback da fonte acorda o render, que volta aqui
            // A mesma época do playback: no corte, o caminho normal não
            // invalida o decoder que já está posicionado.
            src->set_epoch(settings.mediaGeneration);
            const VideoStreamInfo info = src->info();
            i64 us = static_cast<i64>(std::llround(l->source_frame(l->start) * 1e6 / fps));
            if (info.fps > 0.0 && !info.preciseFrameTiming) {
                const f64 idx = std::floor(static_cast<f64>(us) * info.fps / 1e6 + 1e-3);
                us = static_cast<i64>(std::llround(idx * 1e6 / info.fps));
            }
            if (info.durationUs > 0) us = std::clamp<i64>(us, 0, last_source_timestamp(info, src->frame_duration_us()));
            bool ready = false;
            (void)src->frame_for(us, &ready);
            if (ready) continue;
            src->cache().set_required_times(&us, 1, src->frame_duration_us() / 2);
            DecodeRequest req;
            req.targetUs = us;
            req.mode = DecodeMode::Still;
            req.direction = 0;
            req.speed = 1.0f;
            src->request(req);
        }
    }
    for (u32 k = used; k < out.plans.size(); ++k) out.plans[k].clear();
    // Track matte: a matte de cada camada neste snapshot (ausente = −1).
    for (RenderLayer& rl : out.layers) {
        if (rl.matteMode == MatteMode::None) continue;
        for (u32 j = 0; j < out.layers.size(); ++j) {
            if (out.layers[j].matteOnly && out.layers[j].id.pack() == rl.matteId) { rl.matteIndex = static_cast<i32>(j); break; }
        }
    }
    if (!out.scenes.empty()) fill_scene_context(comp, time, out, cam3d);
    // A shared exposure clock, with a fixed CPU admission budget. Rigid models
    // share their immutable pose; only deforming models need sampled geometry.
    if (!out.scenes.empty() && comp.motion_blur().enabled && comp.motion_blur().shutterAngle > 0.0f) {
        const MotionBlurSettings& mb = comp.motion_blur();
        // Render ids are salted inside a precomposition. They identify GPU
        // state, whereas the composition table still owns the original ids.
        std::unordered_map<u64, const Layer*> sourceLayers;
        sourceLayers.reserve(order.size());
        for (u32 i = 0; i < order.size(); ++i) sourceLayers.emplace(render_id(order.at(i)).pack(), comp.layer(order.at(i)));
        auto sourceLayer = [&](u64 key) -> const Layer* {
            const auto found = sourceLayers.find(key);
            return found == sourceLayers.end() ? nullptr : found->second;
        };
        u64& stateBudget = exposureBytesRemaining_;
        auto poseBytes = [](const scene3d::SceneInstance& in) {
            const auto& p = in.pose();
            u64 bytes = p.nodeWorld.size() * sizeof(Mat4) + p.jointMatrices.size() * sizeof(Mat4)
                + p.skinJointOffset.size() * sizeof(u32) + p.materials.size() * sizeof(MaterialOverride)
                + p.nodeOpacity.size() * sizeof(f32) + p.nodeFill.size() * sizeof(Vec4)
                + p.morphWeights.size() * sizeof(std::vector<f32>);
            for (const auto& weights : p.morphWeights) bytes += weights.size() * sizeof(f32);
            return bytes;
        };
        const auto cameraAt = [&](f64 offset) {
            return settings.sceneEditor.enabled && !settings.finalQuality ? cam3d
                : camera_for_frac(comp, time.value + offset, out.compWidth, out.compHeight);
        };
        const auto projection = [&](const scene3d::SceneCamera& camera) {
            return camera.imageTransform * scene3d::reverse_z_perspective(camera.fovY,
                static_cast<f32>(out.compWidth) / std::max(1u, out.compHeight), camera.nearZ) * camera.view;
        };
        for (scene3d::SceneFrame& f : out.scenes) {
            f.blurFrames.clear();
            ShutterPlan shutter = shutter_window(mb);
            bool changing = false;
            f64 travel = 0;
            u32 minimum = 1;
            u64 bytesPerSample = sizeof(scene3d::SceneFrame) + f.instances.size() * sizeof(scene3d::SceneInstance)
                + f.lights.size() * sizeof(scene3d::SceneLight);
            u64 poseBytesPerSample = bytesPerSample;
            std::vector<bool> deforming(f.instances.size(), false);
            for (usize i = 0; i < f.instances.size(); ++i) {
                const auto& in = f.instances[i];
                const Layer* layer = sourceLayer(in.layerKey);
                const bool samplesModelTime = layer && in.motionBlur
                    && motion_blur_amount(layer->transform.motionBlurAmount) > 0.f;
                const scene3d::Animation* sampledClip = samplesModelTime && in.asset
                    && layer->model.animationClip >= 0 && layer->model.animationClip < static_cast<i32>(in.asset->animations.size())
                    ? &in.asset->animations[static_cast<usize>(layer->model.animationClip)] : nullptr;
                std::vector<bool> sampledMorphNodes;
                if (sampledClip) for (const auto& channel : sampledClip->channels) {
                    if (channel.path != scene3d::AnimPath::Weights || channel.node < 0
                        || static_cast<usize>(channel.node) >= in.asset->nodes.size()) continue;
                    if (sampledMorphNodes.empty()) sampledMorphNodes.resize(in.asset->nodes.size(), false);
                    sampledMorphNodes[static_cast<usize>(channel.node)] = true;
                }
                // Upload pools retain every submitted sample until the fence.
                // Sharing CPU poses does not share morph vertices or joints on
                // the GPU. Admit those bytes and graph draw/uniform state too.
                const u64 jointBytes = in.pose().jointMatrices.size() * sizeof(Mat4);
                if (jointBytes) bytesPerSample += std::max<u64>(4096, jointBytes);
                if (in.asset) for (usize n = 0; n < in.asset->nodes.size(); ++n) {
                    const auto& node = in.asset->nodes[n];
                    if (!node.inScene) continue;
                    if (node.mesh < 0 || static_cast<usize>(node.mesh) >= in.asset->meshes.size()) continue;
                    const auto& mesh = in.asset->meshes[static_cast<usize>(node.mesh)];
                    const auto& weights = n < in.pose().morphWeights.size() && !in.pose().morphWeights[n].empty()
                        ? in.pose().morphWeights[n] : mesh.morphWeights;
                    bool uploadsMorph = std::any_of(weights.begin(), weights.end(), [](f32 weight) {
                        return std::fabs(weight) > 1e-5f;
                    });
                    // Zero weights use the undeformed GPU mesh. A sampled
                    // weight channel can become active between exposure times,
                    // even when the center pose is zero; admit it conservatively.
                    uploadsMorph |= !sampledMorphNodes.empty() && sampledMorphNodes[n];
                    for (const auto& primitive : mesh.primitives) {
                        bytesPerSample += 4096; // draw lists, instance matrices, material/camera uniforms, shadow/reflection state
                        if (uploadsMorph && !primitive.morphTargets.empty())
                            bytesPerSample += (static_cast<u64>(primitive.positions.size()) * (sizeof(Vec3) + kMeshShadingStride) + 255u) & ~255ull;
                    }
                }
                if (!layer || !in.asset || !in.motionBlur || motion_blur_amount(layer->transform.motionBlurAmount) <= 0.f) continue;
                deforming[i] = (layer->model.animationClip >= 0 && layer->model.animationClip < static_cast<i32>(in.asset->animations.size()))
                    // Texto/forma 3D só deforma quando o conteúdo anima (animadores,
                    // partes): parado, era tratado como deformando e o motion blur
                    // fazia 16 cenas por quadro sem nada mudar (0.2.7).
                    || ((in.asset->textGlyphLayout || in.asset->shapeParts) && !can_share_static_pose(*layer, *in.asset))
                    || scene3d::has_part_effects(*layer);
                for (u32 t = 0; t < layer->tracks.size(); ++t)
                    if (layer->tracks.at(t).property == TrackProperty::MaterialParam && layer->tracks.at(t).animated()) deforming[i] = true;
                changing |= deforming[i];
                if (deforming[i]) {
                    const u64 bytes = poseBytes(in);
                    bytesPerSample += bytes;
                    poseBytesPerSample += bytes;
                }
                const ShutterPlan own = shutter_window(mb, layer->transform.motionBlurAmount);
                const auto& bounds = in.asset->bounds;
                if (!bounds.valid()) { changing = true; continue; }
                for (f32 z : {bounds.min.z, bounds.max.z}) {
                    const Mat4 origin = Mat4::translation(Vec3{bounds.min.x, bounds.min.y, z});
                    travel = std::max(travel, shutter_projected_path(own, bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y,
                        [&](f64 offset) {
                            const Mat4 camera = settings.sceneEditor.enabled && !settings.finalQuality
                                ? scene_editor_projection(out.compWidth, out.compHeight, settings.sceneEditor)
                                : comp_view_projection_frac(comp, time.value + offset, out.compWidth, out.compHeight);
                            return camera * world_3d_frac(comp, *layer, time.value + offset) * layer_from_model(layer->model) * origin;
                        }, previewFactor));
                }
            }
            for (u32 pi : f.particleLayers) if (pi < out.layers.size() && out.layers[pi].particle.blur) changing = true;
            for (u32 pi : f.planeLayers) if (pi < out.layers.size())
                minimum = std::max(minimum, static_cast<u32>(out.layers[pi].blurMatrices.size()));
            const bool batchEligible = settings.boundedSceneExposure && settings.finalQuality
                && prepareDepth_ == 0 && out.nested.empty() && f.planeLayers.empty() && f.particleLayers.empty();
            const u32 desired = capped_shutter_samples(mb, settings.finalQuality,
                effective_quality(settings).motionBlurSamples, travel, changing, ShutterPlan::kHardLimit);
            const u64 batchBudget = std::min(stateBudget, settings.sceneExposureBatchBudgetBytes);
            const bool batchExposure = batchEligible && desired > 1
                && bytesPerSample > batchBudget / desired;
            const u64 admittedBytes = batchExposure ? poseBytesPerSample : bytesPerSample;
            const u32 admitted = static_cast<u32>(std::min<u64>(ShutterPlan::kHardLimit, stateBudget / std::max<u64>(1, admittedBytes)));
            // Orçamento de estado do quadro é FIXO (mesmo em toda tentativa):
            // recusar deixava a cena 3D pesada com motion blur incompleta para
            // sempre e o export desistia em 30 s ("recurso de composicao
            // indisponivel"). Agora a cena usa as amostras que CABEM; sem
            // espaço nem para 2, sai sem desfoque neste grupo — mas sai.
            if (admitted < 2 && minimum <= 1) {
                if (settings.finalQuality && (changing || travel >= .01))
                    AUREA_LOG_WARN("motion blur 3D sem orcamento neste quadro: grupo sem desfoque (%llu bytes por amostra)",
                                   static_cast<unsigned long long>(admittedBytes));
                continue;
            }
            // 0.2.7: sem orçamento nem para 2 amostras, o grupo sai sem desfoque
            // neste quadro (os planos 2D da cena ficam com o desfoque deles).
            // Marcar incompleto fazia o export refazer o quadro por 30 s e desistir.
            if (admitted < 2) {
                if (settings.finalQuality) AUREA_LOG_WARN("motion blur 3D sem orcamento neste quadro (%llu bytes por amostra)",
                                                          static_cast<unsigned long long>(admittedBytes));
                continue;
            }
            if (settings.finalQuality && admitted < std::clamp(mb.samples, 2u, 64u) && (changing || travel >= .01))
                AUREA_LOG_WARN("motion blur 3D reduzido a %u amostras pelo orcamento do quadro (pedido %u)", admitted, mb.samples);
            // Cada amostra é a CENA INTEIRA de novo (sombra, MSAA): no export, o
            // teto da cena e o teto adaptativo (quadro pesado) valem mesmo abaixo
            // do piso da prévia — senão 128 cenas por quadro passavam do prazo
            // da GPU em todo degrau da escada (0.2.7).
            u32 sceneLimit = admitted;
            if (settings.finalQuality && exportSceneBlurCap_ >= 1) sceneLimit = std::min(sceneLimit, exportSceneBlurCap_);
            if (settings.finalQuality && exportBlurCap_ >= 2) sceneLimit = std::min(sceneLimit, exportBlurCap_);
            shutter.count = std::min(sceneLimit, std::max(minimum, capped_shutter_samples(mb, settings.finalQuality,
                effective_quality(settings).motionBlurSamples, travel, changing, sceneLimit)));
            if (shutter.count <= 1) continue;
            stateBudget -= admittedBytes * shutter.count;
            if (batchExposure) out.sceneExposureBatches.push_back(FrameSnapshot::SceneExposureBatch{
                static_cast<u32>(&f - out.scenes.data()), 0, bytesPerSample - poseBytesPerSample});
            std::vector<std::shared_ptr<const scene3d::SceneInstance>> poses;
            poses.reserve(f.instances.size());
            for (auto& base : f.instances) {
                // Transfer ownership once; even the shared pose must not copy
                // an already resident dense model's vectors for every frame.
                auto pose = std::make_shared<scene3d::SceneInstance>(std::move(base));
                base = scene3d::SceneInstance{};
                base.asset = pose->asset; base.assetKey = pose->assetKey; base.world = pose->world;
                base.layerKey = pose->layerKey; base.motionBlur = pose->motionBlur; base.castShadows = pose->castShadows;
                base.doubleSided = pose->doubleSided;
                base.ownEnvironment = pose->ownEnvironment; base.environment = pose->environment;
                // "Trocar textura 3D" fica na instância (não na pose): sem esta
                // cópia os sub-quadros do desfoque saíam com o material original.
                base.nodeTextures = pose->nodeTextures;
                base.sharedPose = pose;
                poses.push_back(std::move(pose));
            }
            for (u32 pi : f.particleLayers) {
                if (pi >= out.layers.size()) continue;
                auto& ps = out.layers[pi].particle;
                ps.subs.resize(shutter.count);
                for (u32 s = 0; s < shutter.count; ++s) {
                    const f64 offset = ps.blur ? shutter.offset(s) * ps.blurAmount : 0.;
                    const auto camera = cameraAt(offset);
                    auto& sub = ps.subs[s]; sub.shift = static_cast<f32>(offset / std::max(1.f, ps.fps));
                    sub.clip = projection(camera);
                    sub.right = Vec4{camera.view.col[0].x,camera.view.col[1].x,camera.view.col[2].x,0};
                    sub.down = Vec4{camera.view.col[0].y,camera.view.col[1].y,camera.view.col[2].y,0};
                }
            }
            // Resample planes on exactly the scene clock. Selecting the nearest
            // sample from a different-sized exposure introduces temporal judder.
            for (u32 pi : f.planeLayers) {
                if (pi >= out.layers.size()) continue;
                auto& plane = out.layers[pi];
                if (plane.blurMatrices.empty()) continue;
                const Layer* layer = sourceLayer(plane.id.pack());
                if (!layer) continue;
                const auto own = shutter_window(mb, layer->transform.motionBlurAmount);
                ShutterPlan plan = own; plan.count = shutter.count;
                plane.blurMatrices.resize(shutter.count);
                for (u32 s = 0; s < shutter.count; ++s) {
                    const f64 t = time.value + plan.offset(s);
                    plane.blurMatrices[s] = (settings.sceneEditor.enabled && !settings.finalQuality
                        ? scene_editor_projection(out.compWidth, out.compHeight, settings.sceneEditor)
                        : comp_view_projection_frac(comp, t, out.compWidth, out.compHeight)) * world_3d_frac(comp, *layer, t) * plane.blurSourceTransform;
                }
            }
            f.blurFrames.resize(shutter.count);
            for (u32 s = 0; s < shutter.count; ++s) {
                auto& sf = f.blurFrames[s];
                sf.subFrame = shutter.offset(s);
                sf.camera = cameraAt(sf.subFrame);
                sf.lights = f.lights; sf.environment = f.environment; sf.post = f.post;
                sf.shadow = f.shadow; sf.floor = f.floor;
                sf.instances.reserve(f.instances.size());
                for (usize i = 0; i < f.instances.size(); ++i) {
                    const auto& base = f.instances[i];
                    scene3d::SceneInstance in;
                    in.asset = base.asset; in.assetKey = base.assetKey; in.world = base.world;
                    in.layerKey = base.layerKey; in.motionBlur = base.motionBlur; in.castShadows = base.castShadows;
                    in.doubleSided = base.doubleSided;
                    in.ownEnvironment = base.ownEnvironment; in.environment = base.environment;
                    in.nodeTextures = base.nodeTextures;
                    in.sharedPose = poses[i];
                    const Layer* layer = sourceLayer(base.layerKey);
                    const f32 amount = layer && in.motionBlur ? motion_blur_amount(layer->transform.motionBlurAmount) : 0.f;
                    const f64 offset = sf.subFrame * amount;
                    if (layer && in.asset && amount > 0) {
                        if (deforming[i]) {
                            in.sharedPose.reset();
                            place_model(comp, *layer, *in.asset, time.value + offset, in, &poseWorkspace_);
                        } else in.world = world_3d_frac(comp, *layer, time.value + offset) * layer_from_model(layer->model);
                    }
                    // A layer with blur off (or amount zero) retains its center
                    // camera too. Other blurred objects in the group cannot smear it.
                    const auto camera = cameraAt(offset);
                    in.cameraOverride = amount != 1.f;
                    in.sampleViewProj = projection(camera); in.sampleView = camera.view;
                    in.sampleCameraPosition = camera.position;
                    sf.instances.push_back(std::move(in));
                }
            }
        }
    }
    // Other groups and sampled glyphs still occupy their immutable CPU state.
    // Admit only uploads that fit beside all of that state at the batch fence.
    for (auto& batch : out.sceneExposureBatches) {
        const u64 budget = std::min(exposureBytesRemaining_, settings.sceneExposureBatchBudgetBytes);
        batch.samplesPerBatch = static_cast<u32>(std::min<u64>(ShutterPlan::kHardLimit,
            budget / std::max<u64>(1, batch.renderBytesPerSample)));
        // Sem espaço para um lote: uma amostra por envio (mais lento, completo).
        if (!batch.samplesPerBatch) batch.samplesPerBatch = 1;
    }
}

void Renderer::fill_scene_context(const Composition& comp, FrameIndex time, FrameSnapshot& out, const scene3d::SceneCamera& camera) noexcept {
    // Câmera: a camada de câmera ATIVA visível no instante (com pais, inclusive
    // nulo 3D — rig de órbita); sem ela, a padrão.
    const scene3d::SceneCamera cam = camera;
    std::vector<scene3d::SceneLight> lights;
    // Camadas 2D em 3D: só as luzes DE VERDADE da composição (inclusive a
    // ambiente), com a intensidade do AE (1 = cor cheia de frente). Nenhuma
    // luz = plano sem iluminação, como no 2D.
    scene3d::PlaneLightBlock planeLight;
    bool planeLit = false;
    u32 planeCount = 0;
    const OrderedIds<LayerId>& order = comp.order();
    for (u32 i = 0; i < order.size(); ++i) {
        const Layer* l = comp.layer(order.at(i));
        if (!l || !l->visible || !l->contains_time(time)) continue;
        const FrameIndex local = l->local_time(time);
        if (l->kind == LayerKind::Light) {
            planeLit = true;
            const Vec3 color{l->tracks.sample_or(TrackProperty::LightColorR, local, l->light.color.x),
                l->tracks.sample_or(TrackProperty::LightColorG, local, l->light.color.y),
                l->tracks.sample_or(TrackProperty::LightColorB, local, l->light.color.z)};
            const f32 intensity = std::max(0.0f, l->tracks.sample_or(TrackProperty::LightIntensity, local, l->light.intensity));
            if (l->light.kind == LightKind::Ambient) {
                planeLight.ambient = Vec4{planeLight.ambient.x + color.x * intensity, planeLight.ambient.y + color.y * intensity,
                                          planeLight.ambient.z + color.z * intensity, 0.0f};
            } else if (planeCount < scene3d::kPlaneMaxLights) {
                const Mat4 w = world_3d(comp, *l, time);
                const Vec3 dir = Vec3{w.col[2].x, w.col[2].y, w.col[2].z}.normalized();   // a luz aponta para +Z da layer
                const f32 outer = std::clamp(l->tracks.sample_or(TrackProperty::LightConeAngle, local, l->light.coneAngle), 1.0f, 179.0f) * 0.5f * kDeg2Rad;
                const f32 inner = outer * (1.0f - std::clamp(l->tracks.sample_or(TrackProperty::LightPenumbra, local, l->light.penumbra), 0.0f, 1.0f));
                const f32 type = l->light.kind == LightKind::Point ? 1.0f : l->light.kind == LightKind::Spot ? 2.0f : 0.0f;
                planeLight.lightPos[planeCount] = type == 0.0f ? Vec4{-dir.x, -dir.y, -dir.z, 0.0f}
                                                               : Vec4{w.col[3].x, w.col[3].y, w.col[3].z, type};
                planeLight.lightColor[planeCount] = Vec4{color.x * intensity, color.y * intensity, color.z * intensity,
                                                         std::isfinite(l->light.range) ? std::max(0.0f, l->light.range) : 0.0f};
                planeLight.lightSpot[planeCount] = Vec4{dir.x, dir.y, dir.z, std::cos(outer)};
                planeLight.lightSpot2[planeCount] = Vec4{std::cos(inner), 0, 0, 0};
                ++planeCount;
            }
        }
        if (l->kind == LayerKind::Camera) {
            continue;   // câmera resolvida em camera_for
        } else if (l->kind == LayerKind::Light && l->light.kind != LightKind::Ambient) {
            scene3d::SceneLight s;
            const Mat4 w = world_3d(comp, *l, time);   // luz filha de nulo 3D acompanha
            (void)local;
            s.kind = l->light.kind == LightKind::Point ? scene3d::LightKindGpu::Point
                   : l->light.kind == LightKind::Spot ? scene3d::LightKindGpu::Spot : scene3d::LightKindGpu::Directional;
            s.position = Vec3{w.col[3].x, w.col[3].y, w.col[3].z};
            s.direction = Vec3{w.col[2].x, w.col[2].y, w.col[2].z}.normalized();   // a luz aponta para +Z da layer
            s.color = Vec3{l->tracks.sample_or(TrackProperty::LightColorR, local, l->light.color.x),
                l->tracks.sample_or(TrackProperty::LightColorG, local, l->light.color.y),
                l->tracks.sample_or(TrackProperty::LightColorB, local, l->light.color.z)};
            s.intensity = std::max(0.0f, l->tracks.sample_or(TrackProperty::LightIntensity, local, l->light.intensity));
            s.range = l->light.range;
            s.outerCone = std::clamp(l->tracks.sample_or(TrackProperty::LightConeAngle, local, l->light.coneAngle), 1.0f, 179.0f) * 0.5f * kDeg2Rad;
            s.innerCone = s.outerCone * (1.0f - std::clamp(l->tracks.sample_or(TrackProperty::LightPenumbra, local, l->light.penumbra), 0.0f, 1.0f));
            s.castShadows = l->light.castShadows;
            s.shadowBias = l->light.shadowBias;
            s.shadowStrength = l->light.shadowStrength;
            // Ponto/spot: candela (KHR_lights_punctual) → mundo em px. A queda
            // 1/d² do shader é em px; × (px/m)² dá a mesma luz que 1/d² em
            // metros na escala física da cena (altura do quadro = 2 m).
            if (s.kind != scene3d::LightKindGpu::Directional) {
                const f32 ppm = scene3d::scene_pixels_per_meter(comp.height());
                s.intensity *= ppm * ppm;
            }
            lights.push_back(s);
        }
    }
    if (lights.empty()) {
        // Sem luz no projeto: uma luz-chave suave do alto à esquerda, na
        // frente. IMPORTOU → APARECE, sem montar iluminação.
        scene3d::SceneLight key;
        key.kind = scene3d::LightKindGpu::Directional;
        key.direction = Vec3{0.45f, 1.0f, 0.75f}.normalized();
        key.color = Vec3{1.0f, 0.97f, 0.92f};
        key.intensity = 1.2f;   // o ambiente de estúdio já tem a caixa de luz principal
        key.castShadows = true;
        lights.push_back(key);
    }
    // Ambiente da composição: intensidade, giro e o HDRI (se houver).
    scene3d::SceneEnvironment env;
    const EnvironmentSettings& es = comp.environment();
    env.intensity = std::max(0.0f, es.intensity);
    env.rotation = es.rotation * kDeg2Rad;
    if (es.hdri.valid() && hdriLookup_) {
        env.hdri = hdriLookup_(hdriCtx_, es.hdri);
        env.hdriKey = es.hdri.pack();
        // Nulo = ainda carregando; vazio = arquivo ausente (fica o estúdio neutro).
        if (!env.hdri) mark_incomplete(__FILE__, __LINE__);
        else if (env.hdri->rgb.empty()) env.hdri.reset();
    } else if (es.studioPreset != 0) {
        // v32: estúdio procedural — o MESMO caminho de um HDRI (chave própria).
        env.hdri = scene3d::studio_hdri(es.studioPreset);
        if (env.hdri) env.hdriKey = scene3d::studio_environment_key(es.studioPreset);
    }
    // v32: chão do grupo 3D.
    scene3d::SceneFloor sceneFloor;
    {
        const FloorSettings& fl = comp.floor();
        sceneFloor.mode = std::min(fl.mode, 2u);
        sceneFloor.color = Vec3{fl.color.r, fl.color.g, fl.color.b};
        sceneFloor.roughness = fl.roughness;
        sceneFloor.reflectivity = fl.reflectivity;
        sceneFloor.contactShadow = fl.contactShadow;
        sceneFloor.fade = fl.fade;
    }
    // Pós do grupo (v31): exposição, tone map, bloom e o nível de qualidade.
    const PostProcessSettings& pp = comp.post_process();
    scene3d::ScenePost post;
    post.toneMapper = std::min(pp.toneMapper, 5u);
    post.bloom = pp.bloom;
    post.bloomIntensity = std::clamp(pp.bloomIntensity, 0.0f, 4.0f);
    post.bloomThreshold = std::max(0.0f, pp.bloomThreshold);
    post.quality = std::min(pp.quality3d, kScene3DQualityCount - 1);
    const f32 groupExposure = std::isfinite(pp.exposure) ? std::clamp(pp.exposure, 0.01f, 64.0f) : 1.0f;
    // Sombra da composição (ShadowSettings): só pisos e viés — o nível de
    // qualidade escolhe mapa e amostras.
    scene3d::SceneShadowSettings shadow;
    {
        const ShadowSettings& sh = comp.shadows();
        shadow.normalBias = std::isfinite(sh.normalBias) ? std::clamp(sh.normalBias, 0.0f, 1.0f) : 0.02f;
        shadow.softShadows = sh.softShadows;
        shadow.pcfSamples = std::min(sh.pcfSamples, 32u);
        // 1024 é o padrão gravado em todo projeto: só um valor MAIOR (escolha
        // explícita) vira piso — senão o calor/AUTO não consegue baixar a 512.
        shadow.mapResolution = sh.mapResolution > 1024u ? std::min(sh.mapResolution, 4096u) : 0u;
    }
    planeLight.cameraPos.w = planeLit ? 1.0f : 0.0f;
    planeLight.lightCount = Vec4{static_cast<f32>(planeCount), 0, 0, 0};
    for (scene3d::SceneFrame& f : out.scenes) {
        f.camera = cam;
        f.lights = lights;
        f.planeLight = planeLight;
        f.shadow = shadow;
        for (scene3d::SceneFrame& b : f.blurFrames) b.shadow = shadow;
        f.environment.exposure = groupExposure;
        f.post = post;
        f.floor = sceneFloor;
        for (scene3d::SceneFrame& b : f.blurFrames) b.floor = sceneFloor;
        f.environment.intensity = env.intensity;
        f.environment.rotation = env.rotation;
        f.environment.hdri = env.hdri;
        f.environment.hdriKey = env.hdriKey;
        f.environment.backgroundBlur = std::isfinite(es.backgroundBlur) ? std::clamp(es.backgroundBlur, 0.0f, 1.0f) : 0.0f;
    }
}

// =============================================================================
// Fase 2 — render (sem lock)
// =============================================================================
// Ambiente POR OBJETO (v22): o objeto escolhe entre o do projeto e o dele. O
// estado é do objeto; os mapas na GPU são compartilhados por HDRI (ver
// SceneRenderer::environment_set).
void Renderer::fill_object_environment(const Layer& l, scene3d::SceneInstance& inst) noexcept {
    if (l.environmentSource != static_cast<u32>(Layer::EnvironmentSource::Custom)) return;
    scene3d::SceneEnvironment e;
    e.intensity = std::max(0.0f, l.environmentIntensity);
    e.exposure = std::max(0.01f, l.environmentExposure);
    e.rotation = l.environmentRotation * kDeg2Rad;
    const AssetId hdri = AssetId::unpack(l.environmentAsset);
    if (const u32 preset = scene3d::studio_preset_of_key(l.environmentAsset); preset != 0) {
        // v32: estúdio procedural no objeto (a chave do preset no lugar do asset).
        e.hdri = scene3d::studio_hdri(preset);
        if (e.hdri) e.hdriKey = l.environmentAsset;
    } else if (hdri.valid() && hdriLookup_) {
        e.hdri = hdriLookup_(hdriCtx_, hdri);
        e.hdriKey = hdri.pack();
        if (!e.hdri) mark_incomplete(__FILE__, __LINE__);
        else if (e.hdri->rgb.empty()) e.hdri.reset();
    }
    inst.environment = std::move(e);
    inst.ownEnvironment = true;
}
#include "RendererSceneExposure.inl"
#include "RendererSceneTargets.inl"
#include "RendererGraphSegments.inl"
#include "RendererPreviewGraph.inl"

void Renderer::upload_glyphs(FrameSnapshot& snap) noexcept {
    // Todos os snapshots (pré-composições incluídas) num buffer só do quadro.
    usize total = 0;
    std::vector<FrameSnapshot*>& all = snapAll_;       // listas do renderer: sem alocar por quadro
    std::vector<FrameSnapshot*>& stack = snapStack_;
    all.clear();
    stack.assign(1, &snap);
    while (!stack.empty()) {
        FrameSnapshot* s = stack.back();
        stack.pop_back();
        s->glyphBase = static_cast<u32>(total);
        total += s->glyphs.size();
        all.push_back(s);
        for (auto& c : s->nested) if (c) stack.push_back(c.get());
    }
    glyphFrameBuf_ = BufferHandle{};
    if (total == 0) return;
    const u32 slot = glyphSlot_++ % kGlyphRing;
    const usize bytes = total * sizeof(GlyphInstance);
    if (glyphCap_[slot] < bytes) {
        if (glyphBuf_[slot].valid()) backend_->destroy_buffer(glyphBuf_[slot]);
        BufferDesc bd;
        bd.bytes = std::max<usize>(bytes * 2, 1024 * sizeof(GlyphInstance));
        bd.usage = BufferUsage::Storage;
        bd.access = MemoryAccess::Upload;
        bd.debugName = "glifos";
        auto b = backend_->create_buffer(bd);
        glyphBuf_[slot] = b.ok() ? *b : BufferHandle{};
        glyphCap_[slot] = b.ok() ? bd.bytes : 0;
    }
    void* ptr = nullptr;
    if (!glyphBuf_[slot].valid() || !backend_->map_buffer(glyphBuf_[slot], ptr).ok() || !ptr) { mark_incomplete(__FILE__, __LINE__); return; }
    auto* dst = static_cast<GlyphInstance*>(ptr);
    for (FrameSnapshot* s : all) std::copy(s->glyphs.begin(), s->glyphs.end(), dst + s->glyphBase);
    backend_->unmap_buffer(glyphBuf_[slot]);
    glyphFrameBuf_ = glyphBuf_[slot];
}

void Renderer::upload_vectors(FrameSnapshot& snap) noexcept {
    // Todas as malhas do quadro (pré-composições incluídas) num buffer só.
    usize total = 0;
    std::vector<FrameSnapshot*>& all = snapAll_;       // listas do renderer: sem alocar por quadro
    std::vector<FrameSnapshot*>& stack = snapStack_;
    all.clear();
    stack.assign(1, &snap);
    while (!stack.empty()) {
        FrameSnapshot* s = stack.back();
        stack.pop_back();
        s->vecBase = static_cast<u32>(total);
        total += s->vec.size();
        all.push_back(s);
        for (auto& c : s->nested) if (c) stack.push_back(c.get());
    }
    vecFrameBuf_ = BufferHandle{};
    if (total == 0) return;
    const u32 slot = vecSlot_++ % kGlyphRing;
    const usize bytes = total * sizeof(Vec4);
    if (vecCap_[slot] < bytes) {
        if (vecBuf_[slot].valid()) backend_->destroy_buffer(vecBuf_[slot]);
        BufferDesc bd;
        bd.bytes = std::max<usize>(bytes * 2, 4096 * sizeof(Vec4));
        bd.usage = BufferUsage::Storage;
        bd.access = MemoryAccess::Upload;
        bd.debugName = "vetores";
        auto b = backend_->create_buffer(bd);
        vecBuf_[slot] = b.ok() ? *b : BufferHandle{};
        vecCap_[slot] = b.ok() ? bd.bytes : 0;
    }
    void* ptr = nullptr;
    if (!vecBuf_[slot].valid() || !backend_->map_buffer(vecBuf_[slot], ptr).ok() || !ptr) { mark_incomplete(__FILE__, __LINE__); return; }
    auto* dst = static_cast<Vec4*>(ptr);
    for (FrameSnapshot* s : all) std::copy(s->vec.begin(), s->vec.end(), dst + s->vecBase);
    backend_->unmap_buffer(vecBuf_[slot]);
    vecFrameBuf_ = vecBuf_[slot];
}

void Renderer::flush_uploads() noexcept {
    usize retained = 0;
    for (usize i = 0; i < uploads_.size(); ++i) {
        PendingUpload& up = uploads_[i];
        const auto desc = backend_->texture_desc(up.texture);
        if (!desc.width) continue; // trimmed since the failed attempt
        // Video/atlas preparation may have queued a newer version while an
        // earlier upload was waiting for memory. Retain only the newest copy.
        bool superseded = false;
        for (usize j = i + 1; j < uploads_.size(); ++j)
            if (uploads_[j].texture == up.texture) { superseded = true; break; }
        if (superseded) continue;
        const u64 pixelBytes = desc.bytes_per_pixel();
        const u64 rowBytes = static_cast<u64>(desc.width) * pixelBytes;
        const u64 stride = up.bytesPerRow ? up.bytesPerRow : rowBytes;
        // The backend accepts a raw pointer, so only this owner can check the
        // allocation extent. Compare by division to avoid hostile-size overflow.
        const bool enough = desc.height && pixelBytes && stride >= rowBytes && stride % pixelBytes == 0
            && rowBytes <= up.data.size()
            && static_cast<u64>(desc.height - 1) <= (up.data.size() - rowBytes) / stride;
        if (!enough) AUREA_LOG_ERROR("upload com pixels insuficientes: %s %u x %u, passo %u, bytes %zu",
            desc.debugName ? desc.debugName : "textura", desc.width, desc.height, up.bytesPerRow, up.data.size());
        const Status s = enough ? backend_->upload_texture(up.texture, up.data.data(), up.bytesPerRow)
                                : Status{Errc::InvalidArgument, "pixels insuficientes para a textura"};
        // Sem blit linear no formato: fica sem mips (correto, só mais serrilhado).
        if (s.ok() && up.mips && !backend_->generate_mipmaps(up.texture).ok())
            AUREA_LOG_WARN("3D: mips nao gerados para a textura trocada");
        if (!s.ok()) {
            mark_incomplete(__FILE__, __LINE__);
            AUREA_LOG_WARN("upload de textura falhou: %s", s.message().data());
            if (retained != i) uploads_[retained] = std::move(up);
            ++retained;
        }
    }
    uploads_.resize(retained);
}

bool Renderer::build_video_source(const RenderLayer& layer, u32 layerIndex, u32 w, u32 h,
                                  FGTexture target, u64 frameNumber) noexcept {
    return build_video_source(layer, layerIndex, w, h, target, frameNumber, layer.source.frame.get());
}

bool Renderer::build_video_source(const RenderLayer& layer, u32 layerIndex, u32 w, u32 h,
                                  FGTexture target, u64 frameNumber, DecodedFrame* f) noexcept {
    if (!f || !f->width || !f->height) return false;
    (void)layerIndex;

    YuvUniforms u{};
    f32 kr = 0.2126f, kb = 0.0722f;
    f->color.coefficients(kr, kb);
    u.coeffs = Vec4{kr, kb, f->color.fullRange ? 1.0f : 0.0f, static_cast<f32>(f->color.bitDepth)};
    const bool hdr = f->color.hdr();
    // HDR no preview SDR: tone map com pico de 1000 nits (o padrão de
    // masterização dos celulares) sobre o branco de referência de 203 nits.
    u.transfer = Vec4{static_cast<f32>(static_cast<u8>(f->color.transfer)),
                      static_cast<f32>(static_cast<u8>(f->color.primaries)),
                      hdr ? 1.0f : 0.0f, 1000.0f / 203.0f};
    const f32 cw = static_cast<f32>(std::max(1u, f->width));
    const f32 ch = static_cast<f32>(std::max(1u, f->height));
    const f32 vw = static_cast<f32>(f->visibleWidth ? f->visibleWidth : f->width);
    const f32 vh = static_cast<f32>(f->visibleHeight ? f->visibleHeight : f->height);
    u.crop = Vec4{static_cast<f32>(f->cropLeft) / cw, static_cast<f32>(f->cropTop) / ch, vw / cw, vh / ch};
    switch (f->rotation) {
        case 90:  u.rot = Vec4{0.0f, -1.0f, 1.0f, 0.0f}; break;
        case 180: u.rot = Vec4{-1.0f, 0.0f, 0.0f, -1.0f}; break;
        case 270: u.rot = Vec4{0.0f, 1.0f, -1.0f, 0.0f}; break;
        default:  u.rot = Vec4{1.0f, 0.0f, 0.0f, 1.0f}; break;
    }
    const f32 dispW = (f->rotation == 90 || f->rotation == 270) ? vh : vw;
    const bool taps = dispW / static_cast<f32>(std::max(1u, w)) >= 2.0f;
    // Tamanho de um pixel de SAÍDA em uv da fonte (já sem a rotação, que só
    // troca os eixos: a caixa de redução é quadrada o bastante para isso).
    u.texel = Vec4{1.0f / cw, 1.0f / ch, (vw / cw) / static_cast<f32>(std::max(1u, (f->rotation % 180) ? h : w)),
                   (vh / ch) / static_cast<f32>(std::max(1u, (f->rotation % 180) ? w : h))};

    if (f->hardwareBuffer) {
        // ZERO-COPY: o AHardwareBuffer do decoder vira textura sem passar pela
        // CPU. O backend cacheia a importação por buffer (o ImageReader
        // recicla um conjunto fixo), então importar custa ~0 em regime.
        ExternalImageDesc ext;
        ext.nativeHandle = f->hardwareBuffer;
        ext.width = f->width;
        ext.height = f->height;
        ext.format = f->format;
        ext.matrix = f->color.matrix;
        ext.fullRange = f->color.fullRange;
        auto imported = backend_->import_external_image(ext);
        if (imported.ok()) {
            u.sampling = Vec4{0.0f, 1.0f, taps ? 1.0f : 0.0f, imported->rgb ? 1.0f : 0.0f};
            PipelineKey key = PipelineKey::fullscreen(ShaderId::video_yuv_external_frag, kWorkFormat);
            key.immutableSampler = imported->sampler.id;
            auto pipe = shaders_.pipeline(key);
            if (pipe.ok()) {
                TextureDesc d;
                d.width = f->width;
                d.height = f->height;
                d.format = SurfaceFormat::RGBA8;
                const FGTexture extTex = graph_.import_texture("frame-do-decoder", imported->texture, d);
                void* ubo = arena_.alloc(sizeof(u), 16);
                if (!ubo) return false;
                std::memcpy(ubo, &u, sizeof(u));
                struct Cap { PipelineHandle p; FGTexture t; u64 sampler; void* ubo; } cap{*pipe, extTex, imported->sampler.id, ubo};
                const u32 pass = graph_.add_raster_pass("cor-do-video", PassStage::Decode, target, LoadOp::DontCare,
                                                        Vec4{0, 0, 0, 0}, [cap](PassContext& pc) {
                    pc.cmds.bind_pipeline(cap.p);
                    pc.cmds.bind_texture(0, pc.texture(cap.t), SamplerHandle{cap.sampler});
                    pc.cmds.set_uniforms(cap.ubo, sizeof(YuvUniforms));
                    pc.cmds.draw(3);
                });
                if (pass == kInvalidIndex) return false;
                graph_.read(pass, extTex);
                lastZeroCopy_ = true;
                return true;
            }
        }
        AUREA_LOG_WARN("importacao zero-copy indisponivel; tentando planos de CPU");
    }

    // FALLBACK: planos na CPU (aparelho sem importação de AHardwareBuffer, ou
    // host de testes). Uma textura por plano, persistente por layer, sobe a
    // cada frame novo.
    if (!f->prepare_cpu_planes()) return false;
    // RGBA da CPU: o vídeo de sempre é RGBA8 opaco; a sequência de imagens
    // (ImageSequence.hpp) também traz RGBA16F (PNG 16 bits, EXR) e alfa.
    const bool halfRgba = f->format == PixelFormat::RGBA16F;
    const bool rgb = f->format == PixelFormat::RGBA8 || halfRgba;
    // Com alfa (ou meia precisão, que pode passar de 1): rgba_to_linear, que
    // mantém o alfa. A conversão de vídeo força alfa 1 e corta acima do branco.
    const bool keepAlpha = rgb && (halfRgba || f->alpha != FrameAlpha::Opaque);
    const bool tenBit = f->format == PixelFormat::P010;
    const bool halfP010 = tenBit && !backend_->capabilities().r16UnormSampled;
    const bool threePlane = f->format == PixelFormat::YUV420P;
    const bool twoPlane = f->format == PixelFormat::NV12 || f->format == PixelFormat::NV21 || tenBit;
    if (!rgb && !threePlane && !twoPlane) return false;
    const u32 planes = rgb ? 1u : (threePlane ? 3u : 2u);
    if (f->planeCount < planes) return false;
    // Check the published layout before any driver reads it. In particular,
    // an RGBA row is four bytes per coded pixel, including native row padding.
    for (u32 p = 0; p < planes; ++p) {
        const u64 width = p == 0 ? f->width : (static_cast<u64>(f->width) + 1) / 2;
        const u64 bytesPerPixel = rgb ? (halfRgba ? 8u : 4u) : ((p && !threePlane ? 2u : 1u) * (tenBit ? 2u : 1u));
        if (!f->planes[p] || f->strides[p] < width * bytesPerPixel) return false;
    }
    // O quadro seguinte da mistura sobe em texturas próprias (senão o upload
    // dele sobrescreveria o do atual antes do passe de cor) — e o mesmo vale
    // para as fontes do RGB no tempo, que são três quadros da MESMA camada no
    // mesmo frame. Sem o sal por fonte, as três dividiriam uma textura só e o
    // passe de junção leria três vezes o último quadro que subiu: o efeito
    // pareceria funcionar (dois canais "certos" por coincidência) e estaria
    // errado no único canal que ele existe para mudar.
    u64 salt = 0;
    if (sliceSalt_) {
        // TimeSlice: cada faixa convertida no mesmo quadro tem planos próprios
        // (o upload de uma não pode sobrescrever o da outra antes do passe).
        salt = sliceSalt_;
    } else if (f == layer.source.frameB.get()) {
        salt = 0x8000000000000000ull;
    } else if (f == layer.source.historyFrame.get() && f != layer.source.frame.get()) {
        salt = 0x4000000000000000ull;   // Detectar movimento: o quadro anterior, textura própria
    } else if (f != layer.source.frame.get()) {
        for (u32 c = 0; c < layer.source.channelCount; ++c) {
            if (f == layer.source.channel[c].frame.get()) { salt = 0x00C0FFEE00ull + c; break; }
        }
    }
    const u64 key = layer.id.pack() ^ salt;
    PlanarTextures& pt = planar_[key];
    if (pt.width != f->width || pt.height != f->height || pt.format != f->format || pt.halfP010 != halfP010) {
        pt.contentId = 0;
        for (TextureHandle& t : pt.plane) {
            if (t.valid()) backend_->destroy_texture(t);
            t = TextureHandle{};
        }
        pt.width = pt.height = 0;
        for (u32 p = 0; p < planes; ++p) {
            TextureDesc d;
            d.width = p == 0 ? f->width : (f->width + 1) / 2;
            d.height = p == 0 ? f->height : (f->height + 1) / 2;
            d.format = rgb ? (halfRgba ? SurfaceFormat::RGBA16F : SurfaceFormat::RGBA8)
                          : (p == 0 || threePlane ? (tenBit ? (halfP010 ? SurfaceFormat::R16F : SurfaceFormat::R16) : SurfaceFormat::R8)
                                                : (tenBit ? (halfP010 ? SurfaceFormat::RG16F : SurfaceFormat::RG16) : SurfaceFormat::RG8));
            d.sampled = true;
            d.transferDst = true;
            d.debugName = "plano-de-video";
            auto tex = backend_->create_texture(d);
            if (!tex.ok()) {
                for (TextureHandle& created : pt.plane) {
                    if (created.valid()) backend_->destroy_texture(created);
                    created = TextureHandle{};
                }
                return false;
            }
            pt.plane[p] = *tex;
        }
        // Publish dimensions only when the full plane set exists. Allocation
        // failure must retry on the next frame, even at the same resolution.
        pt.width = f->width;
        pt.height = f->height;
        pt.format = f->format;
        pt.halfP010 = halfP010;
    }
    pt.lastFrame = frameNumber;
    if (pt.contentId != f->content_id()) {
        // One temporary plane shared by the Y/CbCr uploads. Nothing is pinned
        // per layer or decoded frame, and allocation failure is recoverable.
        // upload_texture consumes/copies CPU data before it returns on every
        // backend; GPU submissions retain their own transfer storage.
        std::unique_ptr<u16[]> halfPlane;
        media::P010Plane halfSources[2];
        media::P010HalfUploadPlan halfPlans[2];
        usize halfSamples = 0;
        if (halfP010) {
            for (u32 p = 0; p < planes; ++p) {
                halfSources[p] = {f->planes[p], p == 0 ? f->width : f->width / 2 + f->width % 2,
                    p == 0 ? f->height : f->height / 2 + f->height % 2, p == 0 ? 1u : 2u, f->strides[p]};
                if (!media::plan_p010_half_upload(halfSources[p], halfPlans[p]).ok()) return false;
                halfSamples = std::max(halfSamples, halfPlans[p].samples);
            }
            halfPlane.reset(new (std::nothrow) u16[halfSamples]);
            if (!halfPlane) return false;
        }
        for (u32 p = 0; p < planes; ++p) {
            if (!f->planes[p]) return false;
            const u64 uploadStart = monotonic_ns();
            const void* bytes = f->planes[p];
            u32 stride = f->strides[p];
            if (halfP010) {
                if (!media::convert_p010_half_plane(halfSources[p], {halfPlane.get(), halfSamples}).ok()) return false;
                bytes = halfPlane.get(); stride = halfPlans[p].rowBytes;
            }
            const Status uploaded = backend_->upload_texture(pt.plane[p], bytes, stride);
            videoUploadMs_ += static_cast<f32>((monotonic_ns() - uploadStart) * 1e-6);
            if (!uploaded.ok()) return false;
            videoPlaneUploadBytes_ += static_cast<u64>(stride) * (p == 0 ? f->height : (f->height + 1) / 2);
        }
        // Publish only after every plane succeeds; a partial upload must retry.
        pt.contentId = f->content_id();
    }

    if (keepAlpha) {
        // Linear e pré-multiplicado, como o resto do espaço de trabalho: alfa
        // reto é multiplicado aqui; EXR (já linear e pré-multiplicado) passa
        // direto. Sequência não tem recorte nem rotação de contêiner.
        struct { Vec4 flags; Vec4 texel; } rp{
            Vec4{f->color.transfer == TransferFunction::Linear ? 1.0f : 0.0f,
                 f->alpha == FrameAlpha::Premultiplied ? 1.0f : 0.0f,
                 f->alpha == FrameAlpha::Opaque ? 1.0f : 0.0f, taps ? 1.0f : 0.0f},
            Vec4{1.0f / cw, 1.0f / ch, 1.0f / static_cast<f32>(std::max(1u, w)), 1.0f / static_cast<f32>(std::max(1u, h))}};
        EffectBuildContext actx(graph_, shaders_, arena_, *this, kWorkFormat, 4096);
        return actx.fullscreen_pass("cor-da-sequencia", PassStage::Decode, target, ShaderId::video_rgba_to_linear_frag,
                                    {PassTexture{{}, pt.plane[0], CommonSampler::LinearClamp}}, &rp, sizeof(rp)) != kInvalidIndex;
    }

    const f32 layoutKind = f->format == PixelFormat::NV21 ? 1.0f : (threePlane ? 2.0f : 0.0f);
    // P010 amostrado como unorm16: código de 10 bits = amostra * 65535 / (64 * 1023).
    const f32 codeScale = halfP010 ? media::kP010HalfCodeScale : (tenBit ? 65535.0f / (64.0f * 1023.0f) : 1.0f);
    u.sampling = Vec4{halfP010 ? 3.0f : layoutKind, codeScale, taps ? 1.0f : 0.0f, rgb ? 1.0f : 0.0f};

    EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat, 4096);
    const SamplerHandle lin = shaders_.sampler(CommonSampler::LinearClamp);
    (void)lin;
    return ctx.fullscreen_pass("cor-do-video", PassStage::Decode, target, ShaderId::video_yuv_planar_frag,
                               {PassTexture{{}, pt.plane[0], CommonSampler::LinearClamp},
                                PassTexture{{}, rgb ? pt.plane[0] : pt.plane[1], CommonSampler::LinearClamp},
                                PassTexture{{}, rgb ? pt.plane[0] : (threePlane ? pt.plane[2] : pt.plane[1]), CommonSampler::LinearClamp}},
                               &u, sizeof(u)) != kInvalidIndex;
}

FGTexture Renderer::video_flow(u64 layerKey, u64 pairKey, FGTexture a, FGTexture b, u32 w, u32 h, f32 baseSide, u32& baseW, u32& baseH,
                               u64 frameNumber) noexcept {
    // Base da pirâmide com o lado maior ≤ 384 (o flow não precisa de 4K); o
    // Flow Retime/Flow Motion Blur pedem 256 (rápido) ou 576 (melhor).
    const f32 side = baseSide > 0.0f ? std::clamp(baseSide, 128.0f, 1024.0f) : 384.0f;
    const f32 base = std::max(96.0f, side * std::clamp(heavyQ_.flow, 0.25f, 1.0f));
    const f32 s = std::min(1.0f, base / static_cast<f32>(std::max(w, h)));
    u32 lw = std::max(8u, static_cast<u32>(std::lround(static_cast<f32>(w) * s)));
    u32 lh = std::max(8u, static_cast<u32>(std::lround(static_cast<f32>(h) * s)));
    baseW = lw;
    baseH = lh;
    TextureDesc fd;
    fd.width = lw;
    fd.height = lh;
    fd.format = kWorkFormat;
    fd.sampled = true;
    fd.renderTarget = true;
    fd.debugName = "flow-cache";
    FlowCache& fc = flowCache_[layerKey];
    fc.lastFrame = frameNumber;
    if (fc.width != lw || fc.height != lh) {
        for (TextureHandle& t : fc.tex) {
            if (!t.valid()) continue;
            backend_->destroy_texture(t);
            heavyStats_.flowCacheBytes -= std::min<u64>(heavyStats_.flowCacheBytes, static_cast<u64>(fc.width) * fc.height * 8);
        }
        fc = FlowCache{};
        fc.width = lw;
        fc.height = lh;
        fc.lastFrame = frameNumber;
    }
    for (u32 i = 0; i < 2 && flowCacheEnabled_; ++i) {
        if (fc.tex[i].valid() && fc.key[i] == pairKey) {
            ++flowHits_;
            ++heavyStats_.flowCacheHits;
            return graph_.import_texture("flow-cache", fc.tex[i], fd);
        }
    }
    ++flowMisses_;
    ++heavyStats_.flowComputed;
    const u32 slot = fc.next;
    fc.next ^= 1u;
    if (!fc.tex[slot].valid()) {
        // Orçamento do cache (todas as camadas): antes de criar, solta as
        // camadas paradas há mais tempo.
        trim_flow_cache(layerKey, static_cast<u64>(lw) * lh * 8);
        auto t = backend_->create_texture(fd);
        if (!t.ok()) return FGTexture{};
        fc.tex[slot] = *t;
        heavyStats_.flowCacheBytes += static_cast<u64>(lw) * lh * 8;   // RGBA16F
    }
    // A chave só vale com os passes gravados: um fluxo que falhou não pode
    // ser servido do cache como válido nos quadros seguintes.
    fc.key[slot] = 0;
    bool recorded = true;
    EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat, 4096);
    FGTexture levels[6];
    u32 sizes[6][2];
    levels[0] = ctx.texture("flow-nivel", lw, lh);
    sizes[0][0] = lw;
    sizes[0][1] = lh;
    const Vec4 lumaParams{1.0f / static_cast<f32>(lw), 1.0f / static_cast<f32>(lh), 0, 0};
    recorded &= ctx.fullscreen_pass("flow-luma", PassStage::Decode, levels[0], ShaderId::video_flow_luma_frag,
                                    {PassTexture{a}, PassTexture{b}}, &lumaParams, sizeof(lumaParams)) != kInvalidIndex;
    u32 n = 1;
    while (n < 6 && std::min(lw, lh) >= 12) {
        const u32 nw = (lw + 1) / 2, nh = (lh + 1) / 2;
        struct { Vec4 uvMap; Vec4 texel; } dp{Vec4{1, 1, 0, 0}, Vec4{1.0f / static_cast<f32>(lw), 1.0f / static_cast<f32>(lh), 0, 0}};
        levels[n] = ctx.texture("flow-nivel", nw, nh);
        recorded &= ctx.fullscreen_pass("flow-reduz", PassStage::Decode, levels[n], ShaderId::effects_downsample_frag,
                                        {PassTexture{levels[n - 1]}}, &dp, sizeof(dp)) != kInvalidIndex;
        sizes[n][0] = lw = nw;
        sizes[n][1] = lh = nh;
        ++n;
    }
    // 0.1.8: os DOIS sentidos (atual → seguinte e seguinte → atual), cada nível
    // regularizado pela confiança e pela luminância (flow_smooth.frag). O
    // quadro intermediário usa os dois para saber quem aparece e quem some
    // (flow_warp.frag); o nível base dos dois vai junto para o cache (RG/BA).
    FGTexture baseFlow[2]{};
    for (u32 dir = 0; dir < 2 && recorded; ++dir) {
        FGTexture flow{};
        bool haveFlow = false;
        for (u32 i = n; i-- > 0;) {
            const f32 fw = static_cast<f32>(sizes[i][0]), fh = static_cast<f32>(sizes[i][1]);
            struct { Vec4 texel; Vec4 flags; } lp{Vec4{1.0f / fw, 1.0f / fh, fw, fh},
                                                  Vec4{haveFlow ? 1.0f : 0.0f, 6.0f, static_cast<f32>(dir), 0}};
            const FGTexture raw = ctx.texture("flow-bruto", sizes[i][0], sizes[i][1]);
            recorded &= ctx.fullscreen_pass("flow-lk", PassStage::Decode, raw, ShaderId::video_flow_lk_frag,
                                            {PassTexture{levels[i]}, PassTexture{haveFlow ? flow : levels[i]}}, &lp, sizeof(lp)) != kInvalidIndex;
            // Regulariza os níveis de cima (é onde o erro de um nível vira o
            // ponto de partida do seguinte); o base fica com o LK cru e passa
            // pelo preenchimento de oclusão — metade do custo, mesma qualidade.
            if (i == 0) {
                flow = raw;
                haveFlow = true;
                continue;
            }
            struct { Vec4 texel; Vec4 flags; } sp{Vec4{1.0f / fw, 1.0f / fh, fw, fh}, Vec4{static_cast<f32>(dir), 0.06f, 0, 0}};
            const FGTexture smooth = ctx.texture("flow", sizes[i][0], sizes[i][1]);
            recorded &= ctx.fullscreen_pass("flow-suave", PassStage::Decode, smooth, ShaderId::video_flow_smooth_frag,
                                            {PassTexture{raw, {}, CommonSampler::NearestClamp}, PassTexture{levels[i]}}, &sp, sizeof(sp)) != kInvalidIndex;
            flow = smooth;
            haveFlow = true;
        }
        baseFlow[dir] = flow;
    }
    // Oclusão: onde a ida e volta não fecha (o pixel some no outro quadro), o
    // fluxo vem dos vizinhos coerentes (flow_fill.frag), nos dois sentidos.
    FGTexture filled[2]{};
    for (u32 dir = 0; dir < 2 && recorded; ++dir) {
        const f32 fw = static_cast<f32>(sizes[0][0]), fh = static_cast<f32>(sizes[0][1]);
        struct { Vec4 texel; Vec4 flags; } fp{Vec4{1.0f / fw, 1.0f / fh, fw, fh}, Vec4{static_cast<f32>(dir), 0.8f, 0, 0}};
        filled[dir] = ctx.texture("flow-preenchido", sizes[0][0], sizes[0][1]);
        recorded &= ctx.fullscreen_pass("flow-oclusao", PassStage::Decode, filled[dir], ShaderId::video_flow_fill_frag,
                                        {PassTexture{baseFlow[dir], {}, CommonSampler::LinearClamp},
                                         PassTexture{baseFlow[dir ^ 1u], {}, CommonSampler::LinearClamp},
                                         PassTexture{levels[0]}}, &fp, sizeof(fp)) != kInvalidIndex;
    }
    // O nível base dos dois sentidos vai direto para a textura do cache.
    const FGTexture packed = graph_.import_texture("flow-cache", fc.tex[slot], fd);
    recorded &= recorded && ctx.fullscreen_pass("flow-junta", PassStage::Decode, packed, ShaderId::video_flow_pack_frag,
                                                {PassTexture{filled[0], {}, CommonSampler::NearestClamp},
                                                 PassTexture{filled[1], {}, CommonSampler::NearestClamp}}, nullptr, 0) != kInvalidIndex;
    if (!recorded) return FGTexture{};
    fc.key[slot] = pairKey;
    return packed;
}

void Renderer::trim_flow_cache(u64 keepLayer, u64 incomingBytes) noexcept {
    // LRU por camada: o flow de uma camada é um par de texturas pequenas
    // (≤ 384 px), mas 30 camadas com flow num projeto longo somam. Acima do
    // orçamento, sai a camada usada há mais tempo (nunca a que pede agora).
    while (heavyStats_.flowCacheBytes + incomingBytes > flowCacheBudget_) {
        auto victim = flowCache_.end();
        for (auto it = flowCache_.begin(); it != flowCache_.end(); ++it) {
            if (it->first == keepLayer) continue;
            if (victim == flowCache_.end() || it->second.lastFrame < victim->second.lastFrame) victim = it;
        }
        if (victim == flowCache_.end()) break;
        for (TextureHandle& t : victim->second.tex) {
            if (!t.valid()) continue;
            backend_->destroy_texture(t);   // o backend adia a destruição até a GPU soltar
            heavyStats_.flowCacheBytes -= std::min<u64>(heavyStats_.flowCacheBytes,
                                                        static_cast<u64>(victim->second.width) * victim->second.height * 8);
        }
        flowCache_.erase(victim);
        ++heavyStats_.flowCacheEvictions;
    }
}

bool Renderer::build_source(const RenderLayer& layer, u32 layerIndex, bool hasEffects, LayerImage& out,
                            std::vector<FrameRef>& framesUsed, u64 frameNumber, u32 glyphSet) noexcept {
    if (layer.source.kind == LayerSource::Kind::Nested) {
        if (!currentSnap_ || layer.source.nestedIndex >= currentSnap_->nested.size()) return false;
        const FrameSnapshot* child = currentSnap_->nested[layer.source.nestedIndex].get();
        if (!child || !child->target.valid()) return false;
        out.texture = child->target;
        out.region = Rect{0.0f, 0.0f, static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height)};
        // The child was rendered at the composition's working resolution.
        // Projecting/scaling its parent does not resize that texture. Describing
        // it with the parent's texelScale made a neutral mask resample the child
        // at another resolution, breaking thin details and antialiased edges.
        const TextureDesc& actual = graph_.desc(child->target);
        out.width = actual.width;
        out.height = actual.height;
        // Efeitos de tempo: a filha nos outros instantes (ver `prepare`), no
        // mesmo tamanho — a mesma junção/histórico do caminho do vídeo.
        auto other = [&](u32 index) -> FGTexture {
            if (index == ~0u || index >= currentSnap_->nested.size()) return FGTexture{};
            const FrameSnapshot* s = currentSnap_->nested[index].get();
            if (!s || !s->target.valid()) return FGTexture{};
            const TextureDesc& d = graph_.desc(s->target);
            return d.width == actual.width && d.height == actual.height ? s->target : FGTexture{};
        };
        if (layer.source.nestedChannel[0] != ~0u) {
            const FGTexture chan[3] = {other(layer.source.nestedChannel[0]), other(layer.source.nestedChannel[1]),
                                       other(layer.source.nestedChannel[2])};
            bool ok = chan[0].valid() && chan[1].valid() && chan[2].valid();
            if (ok) {
                TextureDesc md = actual;
                md.sampled = true;
                md.renderTarget = true;
                md.transferSrc = false;
                md.debugName = "rgb-no-tempo-pre";
                const FGTexture merged = graph_.create_texture("rgb-no-tempo-pre", md);
                EffectBuildContext rctx(graph_, shaders_, arena_, *this, kWorkFormat,
                                        std::min<u32>(backend_->capabilities().maxTexture2D, 8192));
                ok = rctx.fullscreen_pass("rgb-no-tempo-pre", PassStage::Decode, merged, ShaderId::effects_rgb_merge_frag,
                                          {PassTexture{chan[0], {}, CommonSampler::LinearClamp},
                                           PassTexture{chan[1], {}, CommonSampler::LinearClamp},
                                           PassTexture{chan[2], {}, CommonSampler::LinearClamp}},
                                          nullptr, 0) != kInvalidIndex;
                if (ok) out.texture = merged;
            }
            if (!ok) mark_incomplete(__FILE__, __LINE__);
        }
        if (layer.source.nestedHistory != ~0u) {
            const FGTexture past = other(layer.source.nestedHistory);
            if (past.valid()) out.history = past;
            else mark_incomplete(__FILE__, __LINE__);
        }
        // Aprimorar: a filha nos quadros vizinhos.
        for (const RenderLayer::NeighborSample& n : layer.neighbors) {
            if (out.neighbors.count >= kMaxTemporalNeighbors) break;
            const FGTexture t = other(n.nested);
            if (!t.valid()) { mark_incomplete(__FILE__, __LINE__); continue; }
            out.neighbors.texture[out.neighbors.count] = t;
            out.neighbors.offset[out.neighbors.count] = static_cast<i8>(n.offset);
            ++out.neighbors.count;
        }
        return true;
    }
    if (layer.source.kind == LayerSource::Kind::Scene3D) {
        // O grupo 3D renderiza no tamanho do alvo da composição (resolução de
        // preview incluída) e cobre a composição inteira.
        if (!currentScenes_ || layer.source.sceneGroup >= currentScenes_->size()) return false;
        const scene3d::SceneFrame& group = (*currentScenes_)[layer.source.sceneGroup];
        for (const auto& staged : stagedSceneTargets_) {
            if (staged.owner != currentSnap_ || staged.scene != layer.source.sceneGroup || !staged.texture.valid()) continue;
            const TextureDesc desc = backend_->texture_desc(staged.texture);
            out.texture = graph_.import_texture("3d-captura-concluida", staged.texture, desc);
            out.region = Rect{0, 0, static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height)};
            out.width = desc.width; out.height = desc.height;
            return true;
        }
        if (layer.source.sceneGroup < boundedSceneTextures_.size()
            && boundedSceneTextures_[layer.source.sceneGroup].valid()) {
            const TextureHandle texture = boundedSceneTextures_[layer.source.sceneGroup];
            out.texture = graph_.import_texture("3d-exposicao-concluida", texture, backend_->texture_desc(texture));
            out.region = Rect{0, 0, static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height)};
            out.width = compTargetW_; out.height = compTargetH_;
            return true;
        }
        FGTexture tex{};
        const std::vector<scene3d::ScenePlane>* planes =
            layer.source.sceneGroup < groupPlanes_.size() && !groupPlanes_[layer.source.sceneGroup].empty() ? &groupPlanes_[layer.source.sceneGroup] : nullptr;
        configure_scene_quality(group);
        // Every precomposition has its own environment. Final rendering must
        // resolve each group's maps before its passes capture the handles.
        if (heavyQ_.exportFrame) scene3d_.finish_environment(group.environment);
        struct SceneStatsSink {
            Renderer* r;
            ~SceneStatsSink() {
                const scene3d::SceneStats& st = r->scene3d_.stats();
                r->heavyStats_.lastSceneDrawCalls = st.drawCalls;
                r->heavyStats_.lastSceneShadowDrawCalls = st.shadowDrawCalls;
                r->heavyStats_.lastSceneInstancedDraws = st.instancedDraws;
                r->heavyStats_.lastSceneVisible = st.visiblePrimitives;
                r->heavyStats_.lastSceneCulled = st.culledPrimitives;
                r->heavyStats_.lastSceneTriangles = st.triangles;
                r->heavyStats_.lastShadowMapSize = st.shadowMapSize;
            }
        } sink{this};
        // Partículas 3D do grupo (8.2): billboards no passe da cena, com a
        // câmera de cada (sub)quadro.
        scene3d::SceneParticleDraw* parts = nullptr;
        if (group.blurFrames.empty()) {
            const u32 np = scene_particle_draws(group, group, parts);
            if (!scene3d_.build(graph_, arena_, group, compTargetW_, compTargetH_, frameNumber, tex, planes, parts, np)) return false;
        } else {
            // Desfoque: K cenas no obturador, média aditiva (peso 1/K, cor
            // pré-multiplicada) num alvo do tamanho da composição.
            const u32 k = static_cast<u32>(group.blurFrames.size());
            auto pAdd = shaders_.pipeline(PipelineKey::graphics(
                ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Add));
            if (!pAdd.ok()) return false;
            auto* built = arena_.make<u32>(0);
            if (!built) return false;
            TextureDesc ad;
            ad.width = compTargetW_;
            ad.height = compTargetH_;
            ad.format = kWorkFormat;
            ad.sampled = true;
            ad.renderTarget = true;
            tex = graph_.create_texture("3d-desfoque", ad);
            FGTexture integratedHdr{};
            // Consume each sample before building the next one. Keeping all K
            // outputs alive until a final pass used >1 GB at 1080p/64 samples.
            // Planos com desfoque: cada sub-quadro leva o plano na matriz do
            // mesmo instante do obturador (a cena copia a lista no build).
            bool planeBlur = false;
            if (planes && currentSnap_) {
                for (const scene3d::ScenePlane& p : *planes) {
                    planeBlur |= p.sourceLayer < currentSnap_->layers.size() && !currentSnap_->layers[p.sourceLayer].blurMatrices.empty();
                }
            }
            const Mat4 clipComp = clip_from_comp(static_cast<f32>(currentSnap_ ? currentSnap_->compWidth : layer.source.width),
                                                 static_cast<f32>(currentSnap_ ? currentSnap_->compHeight : layer.source.height));
            for (u32 s = 0; s < k; ++s) {
                FGTexture sample, sampleHdr;
                const std::vector<scene3d::ScenePlane>* samplePlanes = planes;
                if (planeBlur) {
                    blurPlanes_.assign(planes->begin(), planes->end());
                    for (scene3d::ScenePlane& p : blurPlanes_) {
                        if (p.sourceLayer >= currentSnap_->layers.size()) continue;
                        const std::vector<Mat4>& bm = currentSnap_->layers[p.sourceLayer].blurMatrices;
                        if (bm.empty()) continue;
                        const usize si = std::min<usize>(bm.size() - 1, static_cast<usize>(s) * bm.size() / k);
                        p.clipFromLayer = clipComp * bm[si];
                        // A PAREDE PERIÓDICA DO MOTION TILE não é desenhada com
                        // `clipFromLayer`: o quad cobre o quadro inteiro e cada
                        // raio é resolvido pela INVERSA (NDC → camada) gravada
                        // no plano. Essa inversa era a do instante do quadro, e
                        // todas as amostras do obturador desenhavam a parede na
                        // MESMA pose — a média saía igual ao quadro sem desfoque
                        // ("com Motion Tile o motion blur não funciona"). Regrava
                        // com a matriz da amostra, o mesmo produto que o desfoque
                        // 2D usa para mover a camada.
                        if (p.periodic.count)
                            (void)motion_tile::periodic_inverse(bm[si], currentSnap_->compWidth,
                                                               currentSnap_->compHeight, p.periodic);
                        const Vec4 c = p.clipFromLayer * Vec4{p.region.x + p.region.z * 0.5f, p.region.y + p.region.w * 0.5f, p.extrusion.count?p.extrusion.params.x*.5f:0.f, 1};
                        p.viewDepth = c.w;
                    }
                    samplePlanes = &blurPlanes_;
                }
                const u32 np = scene_particle_draws(group, group.blurFrames[s], parts);
                if (!scene3d_.build(graph_, arena_, group.blurFrames[s], compTargetW_, compTargetH_, frameNumber, sample, samplePlanes,
                                    parts, np, nullptr, &sampleHdr)) { mark_incomplete(__FILE__, __LINE__); continue; }
                struct Cap {
                    FGTexture sample; const u32* count; PipelineHandle p; u64 sampler; f32 w; f32 h; bool hdr = false;
                } cap{sample, built, *pAdd, shaders_.sampler(CommonSampler::LinearClamp).id,
                      static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height)};
                const LoadOp load = *built == 0 ? LoadOp::Clear : LoadOp::Load;
                ++*built;
                auto accumulate = [&](FGTexture target, FGTexture source, bool hdr = false) {
                cap.sample = source;
                cap.hdr = hdr;
                const u32 pass = graph_.add_raster_pass("3d-desfoque", PassStage::Composite, target, load, Vec4{0, 0, 0, 0},
                                                        [cap](PassContext& pc) {
                    pc.cmds.bind_pipeline(cap.p);
                    pc.cmds.bind_texture(0, pc.texture(cap.sample), SamplerHandle{cap.sampler});
                    LayerPush push;
                    push.clipFromLayer = clip_from_comp(cap.w, cap.h);
                    push.region = Vec4{0.0f, 0.0f, cap.w, cap.h};
                    push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
                    push.params = Vec4{1.0f / static_cast<f32>(*cap.count), 0, 0, cap.hdr ? -1.f : 0.f};
                    pc.cmds.push_constants(&push, sizeof(push));
                    pc.cmds.draw(6);
                });
                graph_.read(pass, source);
                };
                accumulate(tex, sample);
                if (sampleHdr.valid()) {
                    if (!integratedHdr.valid()) integratedHdr = graph_.create_texture("3d-exposicao-hdr", ad);
                    accumulate(integratedHdr, sampleHdr, true);
                }
            }
            if (*built == 0) return false;
            if (integratedHdr.valid()) {
                const FGTexture encoded = graph_.create_texture("3d-exposicao-resolve", ad);
                struct Encode { FGTexture src; PipelineHandle pipeline; u64 sampler; f32 w; f32 h; } cap{
                    integratedHdr, *pAdd, shaders_.sampler(CommonSampler::LinearClamp).id,
                    static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height)};
                const u32 pass = graph_.add_raster_pass("3d-exposicao-resolve", PassStage::Composite, encoded,
                    LoadOp::Clear, Vec4{}, [cap](PassContext& pc) {
                        pc.cmds.bind_pipeline(cap.pipeline);
                        pc.cmds.bind_texture(0, pc.texture(cap.src), SamplerHandle{cap.sampler});
                        LayerPush push;
                        push.clipFromLayer = clip_from_comp(cap.w, cap.h);
                        push.region = Vec4{0,0,cap.w,cap.h}; push.uvRect = Vec4{0,0,1,1};
                        push.params = Vec4{1,0,0,-2};
                        pc.cmds.push_constants(&push, sizeof(push)); pc.cmds.draw(6);
                    });
                graph_.read(pass, integratedHdr);
                integratedHdr = encoded;
            }
            tex = scene3d_.finish_exposure(graph_, group, compTargetW_, compTargetH_, tex, integratedHdr);
            if (!tex.valid()) return false;
        }
        out.texture = tex;
        out.region = Rect{0.0f, 0.0f, static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height)};
        out.width = compTargetW_;
        out.height = compTargetH_;
        return true;
    }
    const u32 maxTex = std::min<u32>(backend_->capabilities().maxTexture2D, 8192);
    const f32 k = layer.texelScale;
    u32 w = std::max(1u, static_cast<u32>(std::ceil(static_cast<f32>(layer.source.width) * k)));
    u32 h = std::max(1u, static_cast<u32>(std::ceil(static_cast<f32>(layer.source.height) * k)));
    if (w > maxTex || h > maxTex) {
        const f32 r = static_cast<f32>(maxTex) / static_cast<f32>(std::max(w, h));
        w = std::max(1u, static_cast<u32>(static_cast<f32>(w) * r));
        h = std::max(1u, static_cast<u32>(static_cast<f32>(h) * r));
    }
    TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = kWorkFormat;
    d.sampled = true;
    d.renderTarget = true;

    out.region = Rect{0.0f, 0.0f, static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height)};
    out.width = w;
    out.height = h;

    switch (layer.source.kind) {
        case LayerSource::Kind::Video: {
            out.texture = graph_.create_texture("layer-video", d);
            if (!build_video_source(layer, layerIndex, w, h, out.texture, frameNumber)) {
                mark_incomplete(__FILE__, __LINE__);
                videoSourceFailed_ = true;
                return false;
            }
            if (layer.source.frame->hardwareBuffer) framesUsed.push_back(layer.source.frame);

            // RGB NO TEMPO (Fase 7.3 §25): a textura da camada vira a JUNÇÃO
            // de três fontes, uma por canal de cor. Um passe de tela cheia
            // (rgb_merge.frag) lê o mesmo uv nas três — elas cobrem exatamente
            // a mesma área, então não há matriz nenhuma para acertar.
            if (layer.source.channelCount > 0) {
                bool built = true;
                FGTexture chan[LayerSource::kMaxChannelFrames]{};
                for (u32 c = 0; c < layer.source.channelCount && built; ++c) {
                    if (!layer.source.channel[c].frame) { built = false; break; }
                    chan[c] = graph_.create_texture("rgb-no-tempo-canal", d);
                    built = build_video_source(layer, layerIndex, w, h, chan[c], frameNumber,
                                               layer.source.channel[c].frame.get());
                    if (layer.source.channel[c].frame->hardwareBuffer) framesUsed.push_back(layer.source.channel[c].frame);
                }
                if (built && layer.source.channelCount == 3) {
                    const FGTexture merged = graph_.create_texture("rgb-no-tempo", d);
                    EffectBuildContext rctx(graph_, shaders_, arena_, *this, kWorkFormat, maxTex);
                    const u32 pass = rctx.fullscreen_pass(
                        "rgb-no-tempo", PassStage::Decode, merged, ShaderId::effects_rgb_merge_frag,
                        {PassTexture{chan[0], {}, CommonSampler::LinearClamp},
                         PassTexture{chan[1], {}, CommonSampler::LinearClamp},
                         PassTexture{chan[2], {}, CommonSampler::LinearClamp}},
                        nullptr, 0);
                    if (pass != kInvalidIndex) out.texture = merged;
                    else mark_incomplete(__FILE__, __LINE__);
                }
                // Sem os três quadros (decoder atrasado), a camada sai da fonte
                // principal: melhor mostrar o quadro atual do que um buraco.
                // That fallback is presentable, but cannot complete a cached
                // preview or an exact capture/export while GPU uploads retry.
                if (!built || layer.source.channelCount != 3) mark_incomplete(__FILE__, __LINE__);
            }
            // DETECTAR MOVIMENTO: a fonte no instante anterior, na mesma área e
            // densidade; a cadeia de efeitos a leva até o efeito.
            if (layer.source.historyFrame) {
                const FGTexture past = graph_.create_texture("layer-video-anterior", d);
                if (build_video_source(layer, layerIndex, w, h, past, frameNumber, layer.source.historyFrame.get())) {
                    out.history = past;
                    if (layer.source.historyFrame->hardwareBuffer) framesUsed.push_back(layer.source.historyFrame);
                } else mark_incomplete(__FILE__, __LINE__);
            }
            // APRIMORAR: os vizinhos no tempo, na mesma área e densidade; a
            // cadeia de efeitos os leva até o efeito (LayerImage::neighbors).
            for (u32 k = 0; k < layer.neighbors.size() && out.neighbors.count < kMaxTemporalNeighbors; ++k) {
                const FGTexture t = neighbor_video_texture(layer, layerIndex, layer.neighbors[k], k, w, h, frameNumber);
                if (!t.valid()) { mark_incomplete(__FILE__, __LINE__); continue; }
                out.neighbors.texture[out.neighbors.count] = t;
                out.neighbors.offset[out.neighbors.count] = static_cast<i8>(layer.neighbors[k].offset);
                ++out.neighbors.count;
            }
            if (!layer.neighbors.empty()) {
                // Fora da janela: os vizinhos desta camada que não serviram neste
                // quadro nem no anterior saem (memória de ~7 quadros por camada).
                const u64 key = layer.id.pack() ^ kNeighborSliceSalt;
                std::lock_guard<std::mutex> sl(sliceMutex_);
                for (auto it = sliceCache_.begin(); it != sliceCache_.end();) {
                    if (it->layer == key && it->lastFrame + 1 < frameNumber) {
                        if (it->tex.valid()) backend_->destroy_texture(it->tex);
                        it = sliceCache_.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            // Quadro seguinte da fonte: mistura, movimento de pixels e/ou
            // desfoque vetorial (os dois últimos pelo optical flow, em cache).
            if (layer.source.frameB) {
                const FGTexture b = graph_.create_texture("layer-video-seguinte", d);
                if (!build_video_source(layer, layerIndex, w, h, b, frameNumber, layer.source.frameB.get())) {
                    mark_incomplete(__FILE__, __LINE__);
                    return true;
                }
                if (layer.source.frameB->hardwareBuffer) framesUsed.push_back(layer.source.frameB);
                EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat, maxTex);
                const bool needFlow = (layer.source.blendMode == 2 && layer.source.blendT > 0.0f) || layer.source.vectorBlur > 0.0f;
                FGTexture flow{};
                u32 baseW = 0, baseH = 0;
                if (needFlow) {
                    u64 pair = (static_cast<u64>(layer.source.frame->ptsUs) * 1000003ull) ^ static_cast<u64>(layer.source.frameB->ptsUs)
                             ^ (static_cast<u64>(w) << 40) ^ (static_cast<u64>(h) << 52);
                    // A proxy and its original share timestamps and output
                    // dimensions, but not pixels. Decoder reopen/relink must
                    // never reuse motion estimated from the previous source.
                    // Content IDs also remain stable for cached decoded frames.
                    for (const DecodedFrame* source : {layer.source.frame.get(), layer.source.frameB.get()})
                        pair ^= source->content_id() + 0x9e3779b97f4a7c15ull + (pair << 6) + (pair >> 2);
                    pair ^= static_cast<u64>(std::lround(layer.source.flowBase)) << 20;
                    flow = video_flow(layer.id.pack(), pair, out.texture, b, w, h, layer.source.flowBase, baseW, baseH, frameNumber);
                    if (!flow.valid() && layer.source.vectorBlur > 0.0f) mark_incomplete(__FILE__, __LINE__);
                }
                // Smooth Motion sem fluxo válido (passe não compilou, textura do
                // cache não criou) cai na mistura: antes a layer ia preta/lixo.
                bool warped = false;
                if (layer.source.blendT > 0.0f && layer.source.blendMode == 2 && flow.valid()) {
                    const FGTexture moved = graph_.create_texture("layer-video-movimento", d);
                    const Vec4 wp{layer.source.blendT, 1.0f / static_cast<f32>(baseW), 1.0f / static_cast<f32>(baseH), 0};
                    if (ctx.fullscreen_pass("flow-deforma", PassStage::Decode, moved, ShaderId::video_flow_warp_frag,
                                            {PassTexture{out.texture}, PassTexture{b}, PassTexture{flow}}, &wp, sizeof(wp)) != kInvalidIndex) {
                        out.texture = moved;
                        warped = true;
                    }
                }
                if (!warped && layer.source.blendT > 0.0f && layer.source.blendMode >= 1) {
                    // Mistura: alvo novo que LÊ os dois (vídeo opaco: A·(1−t) + B·t).
                    auto pNormal = shaders_.pipeline(PipelineKey::graphics(
                        ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Normal));
                    if (pNormal.ok()) {
                        const FGTexture mix = graph_.create_texture("layer-video-mistura", d);
                        struct Cap { PipelineHandle p; FGTexture a; FGTexture b; u64 sampler; f32 w; f32 h; f32 t; }
                            cap{*pNormal, out.texture, b, shaders_.sampler(CommonSampler::LinearClamp).id, static_cast<f32>(w),
                                static_cast<f32>(h), layer.source.blendT};
                        const u32 pass = graph_.add_raster_pass("mistura-de-quadros", PassStage::Decode, mix, LoadOp::Clear,
                                                                Vec4{0, 0, 0, 0}, [cap](PassContext& pc) {
                            pc.cmds.bind_pipeline(cap.p);
                            LayerPush push;
                            push.clipFromLayer = clip_from_comp(cap.w, cap.h);
                            push.region = Vec4{0.0f, 0.0f, cap.w, cap.h};
                            push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
                            pc.cmds.bind_texture(0, pc.texture(cap.a), SamplerHandle{cap.sampler});
                            push.params = Vec4{1.0f, 0, 0, 0};
                            pc.cmds.push_constants(&push, sizeof(push));
                            pc.cmds.draw(6);
                            pc.cmds.bind_texture(0, pc.texture(cap.b), SamplerHandle{cap.sampler});
                            push.params = Vec4{cap.t, 0, 0, 0};
                            pc.cmds.push_constants(&push, sizeof(push));
                            pc.cmds.draw(6);
                        });
                        graph_.read(pass, out.texture);
                        graph_.read(pass, b);
                        out.texture = mix;
                    }
                }
                if (layer.source.vectorBlur > 0.0f && flow.valid()) {
                    // Borrão ao longo do vetor de cada pixel, obturador centrado no quadro.
                    const FGTexture blurred = graph_.create_texture("layer-video-desfoque-vetorial", d);
                    // Export uses 64 samples; preview follows its adaptive budget.
                    const Vec4 vp{layer.source.vectorBlur, 1.0f / static_cast<f32>(baseW), 1.0f / static_cast<f32>(baseH),
                                  static_cast<f32>(std::clamp<u32>(heavyQ_.flowBlurSamples, 8u, 64u))};
                    if (ctx.fullscreen_pass("desfoque-vetorial", PassStage::Decode, blurred, ShaderId::video_flow_vblur_frag,
                                           {PassTexture{out.texture}, PassTexture{flow}}, &vp, sizeof(vp)) != kInvalidIndex)
                        out.texture = blurred;
                    else mark_incomplete(__FILE__, __LINE__); // Retain valid preview pixels; export retries the requested blur.
                }
            }
            return true;
        }
        case LayerSource::Kind::Text: {
            // Glifos instanciados no atlas SDF, na densidade da layer na tela.
            if (!glyphFrameBuf_.valid() || !glyphAtlas_.valid() || layer.source.glyphCount == 0 || !currentSnap_) return false;
            bool canCrop = !hasEffects;
            if (hasEffects && layerIndex < currentSnap_->plans.size()) {
                const auto& evals = currentSnap_->plans[layerIndex].evals;
                canCrop = !evals.empty() && evals.front().effect &&
                    evals.front().effect->type_id() == effect_type_id(effect_keys::kMotionTile);
            }
            if (boundedTextSurfaceEnabled_ && canCrop) {
                // Animator padding is a virtual canvas, not occupied pixels.
                // Bound ALL shutter instants on the original raster grid, so
                // every sample has the same reusable physical descriptor.
                const Rect canvas = out.region;
                const f32 kx = out.texel_scale_x(), ky = out.texel_scale_y();
                f32 x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
                bool finite = kx > 0.f && ky > 0.f;
                const usize begin = layer.source.glyphFirst;
                const usize count = static_cast<usize>(layer.source.glyphCount) * std::max(1u, layer.source.glyphSets);
                if (begin > currentSnap_->glyphs.size() || count > currentSnap_->glyphs.size() - begin) finite = false;
                if (finite) for (usize n = begin; n < begin + count; ++n) {
                    const GlyphInstance& glyph = currentSnap_->glyphs[n];
                    const f32 margin = glyph.uv.x >= 0.f ? std::max(0.f, glyph.extra.x * 1.5f) : 0.f;
                    for (const f32 gy : {glyph.rect.y - margin, glyph.rect.w + margin})
                        for (const f32 gx : {glyph.rect.x - margin, glyph.rect.z + margin}) {
                            const Vec4 q = glyph.xform * Vec4{gx, gy, 0, 1};
                            const f32 f = layer.source.textPersp.z;
                            const f32 hw = f > 0.f ? std::max(.05f, (f + q.z) / f) : 1.f;
                            const f32 px = layer.source.textPersp.x + (q.x - layer.source.textPersp.x) / hw;
                            const f32 py = layer.source.textPersp.y + (q.y - layer.source.textPersp.y) / hw;
                            if (!std::isfinite(px) || !std::isfinite(py)) { finite = false; continue; }
                            x0 = std::min(x0, px); y0 = std::min(y0, py);
                            x1 = std::max(x1, px); y1 = std::max(y1, py);
                        }
                }
                if (finite && x1 >= x0 && y1 >= y0) {
                    // Two transparent texels retain bilinear sampling at the
                    // crop edges. Outside the crop still samples transparent.
                    const u32 left = static_cast<u32>(std::clamp(std::floor(x0 * kx) - 2.f, 0.f, static_cast<f32>(w - 1)));
                    const u32 top = static_cast<u32>(std::clamp(std::floor(y0 * ky) - 2.f, 0.f, static_cast<f32>(h - 1)));
                    u32 right = static_cast<u32>(std::clamp(std::ceil(x1 * kx) + 2.f, static_cast<f32>(left + 1), static_cast<f32>(w)));
                    u32 bottom = static_cast<u32>(std::clamp(std::ceil(y1 * ky) + 2.f, static_cast<f32>(top + 1), static_cast<f32>(h)));
                    // Multiplos de 16 (como MotionBlurBounds): texto animado
                    // reutiliza o alvo do pool em vez de alocar um por quadro.
                    right = std::min(w, left + ((right - left + 15u) / 16u) * 16u);
                    bottom = std::min(h, top + ((bottom - top + 15u) / 16u) * 16u);
                    if (right - left < w || bottom - top < h) {
                        d.width = right - left; d.height = bottom - top;
                        out.virtualSourceRegion = canvas;
                        out.region = Rect{left / kx, top / ky, d.width / kx, d.height / ky};
                        out.width = d.width; out.height = d.height;
                    }
                }
            }
            auto pipe = shaders_.pipeline(PipelineKey::graphics(ShaderId::text_glyph_vert, ShaderId::text_glyph_frag, kWorkFormat, true,
                                                                BlendMode::Normal));
            if (!pipe.ok()) return false;
            const u32 sets = glyphSet == kInvalidIndex ? std::max(1u, layer.source.glyphSets) : 1u;
            struct Cap { PipelineHandle p; TextureHandle atlas; u64 sampler; BufferHandle buf; Mat4 clip; Vec4 params; u32 count; };
            const Mat4 clip = clip_from_comp(out.region.w, out.region.h)
                * Mat4::translation(Vec3{-out.region.x, -out.region.y, 0});
            const Vec4 persp = layer.source.textPersp;
            auto drawSet = [&](FGTexture target, u32 set) {
                Cap cap{*pipe, glyphAtlas_, shaders_.sampler(CommonSampler::LinearClamp).id, glyphFrameBuf_, clip,
                        Vec4{static_cast<f32>(currentSnap_->glyphBase + layer.source.glyphFirst + set * layer.source.glyphCount), persp.x, persp.y, persp.z},
                        layer.source.glyphCount};
                graph_.add_raster_pass("texto", PassStage::Decode, target, LoadOp::Clear, Vec4{0, 0, 0, 0}, [cap](PassContext& pc) {
                    pc.cmds.bind_pipeline(cap.p);
                    pc.cmds.bind_texture(0, cap.atlas, SamplerHandle{cap.sampler});
                    pc.cmds.bind_storage_buffer(cap.buf);
                    struct { Mat4 m; Vec4 params; } push{cap.clip, cap.params};
                    pc.cmds.push_constants(&push, sizeof(push));
                    pc.cmds.draw(6, cap.count);
                });
            };
            if (sets == 1) {
                out.texture = graph_.create_texture("layer-texto", d);
                drawSet(out.texture, glyphSet == kInvalidIndex ? 0 : glyphSet);
                return true;
            }
            // Keep one full-opacity sample and the accumulator live; average
            // only after compositing all overlapping glyphs in each instant.
            auto pAdd = shaders_.pipeline(PipelineKey::graphics(
                ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Add));
            if (!pAdd.ok()) return false;
            out.texture = graph_.create_texture("layer-texto", d);
            for (u32 si = 0; si < sets; ++si) {
                const FGTexture sample = graph_.create_texture("texto-instante", d);
                drawSet(sample, si);
                struct Acc { FGTexture sample; PipelineHandle p; u64 sampler; f32 w; f32 h; f32 weight; } acc{sample, *pAdd,
                    shaders_.sampler(CommonSampler::LinearClamp).id, static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height), 1.f / sets};
                const u32 pass = graph_.add_raster_pass("texto-desfoque", PassStage::Decode, out.texture,
                    si == 0 ? LoadOp::Clear : LoadOp::Load, Vec4{0, 0, 0, 0}, [acc](PassContext& pc) {
                    pc.cmds.bind_pipeline(acc.p);
                    pc.cmds.bind_texture(0, pc.texture(acc.sample), SamplerHandle{acc.sampler});
                    LayerPush push;
                    push.clipFromLayer = clip_from_comp(acc.w, acc.h);
                    push.region = Vec4{0.0f, 0.0f, acc.w, acc.h};
                    push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
                    push.params = Vec4{acc.weight, 0, 0, 0};
                    pc.cmds.push_constants(&push, sizeof(push));
                    pc.cmds.draw(6);
                });
                graph_.read(pass, sample);
            }
            return true;
        }
        case LayerSource::Kind::Image: {
            if (layer.source.rigCount > 0) {
                // Rig 2D: a imagem linear no tamanho dela (o mesmo caminho,
                // com o cache de densidade) e depois a malha deformada, que
                // amostra a imagem, na textura da caixa da pose.
                if (!vecFrameBuf_.valid() || !currentSnap_) return false;
                auto pipe = shaders_.pipeline(PipelineKey::graphics(ShaderId::rig_mesh_vert, ShaderId::rig_mesh_frag, kWorkFormat, true,
                                                                    BlendMode::Normal));
                if (!pipe.ok()) return false;
                RenderLayer flat = layer;
                flat.source.rigCount = 0;
                flat.source.width = layer.source.rigImageW;
                flat.source.height = layer.source.rigImageH;
                LayerImage img;
                if (!build_source(flat, layerIndex, hasEffects, img, framesUsed, frameNumber) || !img.texture.valid()) return false;
                out.texture = graph_.create_texture("layer-rig", d);
                struct Cap { PipelineHandle p; BufferHandle buf; FGTexture src; u64 sampler; Mat4 clip; Vec4 params; u32 count; } cap{
                    *pipe, vecFrameBuf_, img.texture, shaders_.sampler(CommonSampler::LinearClamp).id,
                    clip_from_comp(static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height)),
                    Vec4{static_cast<f32>(currentSnap_->vecBase + layer.source.rigFirst), 0, 0, 0}, layer.source.rigCount};
                const u32 pass = graph_.add_raster_pass("rig", PassStage::Decode, out.texture, LoadOp::Clear, Vec4{0, 0, 0, 0},
                                                        [cap](PassContext& pc) {
                    pc.cmds.bind_pipeline(cap.p);
                    pc.cmds.bind_storage_buffer(cap.buf);
                    pc.cmds.bind_texture(0, pc.texture(cap.src), SamplerHandle{cap.sampler});
                    struct { Mat4 m; Vec4 params; } push{cap.clip, cap.params};
                    pc.cmds.push_constants(&push, sizeof(push));
                    pc.cmds.draw(cap.count);
                });
                graph_.read(pass, img.texture);
                return true;
            }
            auto it = images_.find(layer.source.image.pack());
            if (it == images_.end()) return false;
            // A imagem não muda: a versão linear nesta densidade, se já existe,
            // é importada pronta — nenhum passe (mesmos pixels: o mesmo passe
            // de conversão, só que feito uma vez).
            ImageTexture& img = it->second;
            TextureDesc ld = d;
            ld.debugName = "imagem-linear";
            ImageTexture::Linear* lin = nullptr;
            for (ImageTexture::Linear& L : img.linear) {
                if (L.tex.valid() && L.w == w && L.h == h) lin = &L;
            }
            if (lin) {
                lin->lastFrame = frameNumber;
                if (lin->fgFrame == frameNumber) {   // outra camada da mesma imagem, neste quadro
                    out.texture = lin->fg;
                    ++imageLinearHits_;
                    return true;
                }
                out.texture = graph_.import_texture("layer-imagem", lin->tex, ld);
                lin->fg = out.texture;
                lin->fgFrame = frameNumber;
                ++imageLinearHits_;
                return out.texture.valid();
            }
            // Tamanho novo: ocupa o slot vazio ou o menos usado (a textura
            // velha sai depois da GPU terminar com ela).
            ImageTexture::Linear* slot = &img.linear[0];
            for (ImageTexture::Linear& L : img.linear) {
                if (!L.tex.valid()) { slot = &L; break; }
                if (L.lastFrame < slot->lastFrame) slot = &L;
            }
            if (slot->tex.valid()) backend_->destroy_texture(slot->tex);
            *slot = ImageTexture::Linear{};
            auto t = backend_->create_texture(ld);
            if (!t.ok()) return false;
            slot->tex = *t;
            slot->w = w;
            slot->h = h;
            slot->lastFrame = frameNumber;
            out.texture = graph_.import_texture("layer-imagem", slot->tex, ld);
            EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat, maxTex);
            const Vec4 flags[2]{Vec4{0.0f, 0.0f, 0.0f, 0.0f}, Vec4{0.0f, 0.0f, 0.0f, 0.0f}};   // rgba_to_linear: flags + texel
            const u32 pass = ctx.fullscreen_pass("imagem", PassStage::Decode, out.texture, ShaderId::video_rgba_to_linear_frag,
                                                 {PassTexture{{}, img.texture, CommonSampler::LinearClamp}},
                                                 &flags, sizeof(flags));
            if (pass == kInvalidIndex) {
                // Pipeline ainda compilando: o slot não fica "pronto" sem conteúdo.
                backend_->destroy_texture(slot->tex);
                *slot = ImageTexture::Linear{};
                return false;
            }
            // Escreve uma textura que sobrevive ao quadro: o passe nunca é
            // podado (senão o próximo quadro importaria lixo).
            graph_.mark_side_effect(pass);
            slot->builtFrame = frameNumber;
            slot->fg = out.texture;
            slot->fgFrame = frameNumber;
            ++imageLinearBuilds_;
            return true;
        }
        case LayerSource::Kind::Solid: {
            // Sólido: um clear, sem shader. Sem efeitos, 1x1 basta — a
            // composição estica. Com efeitos (blur, glow), a textura precisa da
            // densidade da layer: um blur sobre 1 texel não tem vizinhança.
            if (!hasEffects) {
                d.width = d.height = 1;
                out.width = out.height = 1;
            }
            out.texture = graph_.create_texture("layer-solida", d);
            graph_.add_raster_pass("solido", PassStage::Decode, out.texture, LoadOp::Clear, layer.source.solid,
                                   [](PassContext&) {});
            return true;
        }
        case LayerSource::Kind::Shape: {
            out.texture = graph_.create_texture("layer-forma", d);
            EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat, maxTex);
            // Mesma ordem do bloco `Params` de shaders/shape/shape.frag.
            struct ShapeBlock {
                Vec4 size, shape, fill, stroke, extra, more, blob;
            } block{};
            const f32 lw = static_cast<f32>(layer.source.width), lh = static_cast<f32>(layer.source.height);
            block.size = Vec4{lw, lh, lw / static_cast<f32>(w), static_cast<f32>(layer.source.shapeType)};
            block.shape = layer.source.shapeParams;
            block.fill = layer.source.shapeFill;
            block.stroke = layer.source.shapeStroke;
            block.extra = Vec4{layer.source.shapeStrokeWidth, layer.source.shapeExtra.y, layer.source.shapeExtra.z, layer.source.shapeExtra.w};
            block.more = layer.source.shapeMore;
            block.blob = layer.source.shapeBlob;
            return ctx.fullscreen_pass("forma", PassStage::Decode, out.texture, ShaderId::shape_shape_frag, {},
                                       &block, sizeof(block)) != kInvalidIndex;
        }
        case LayerSource::Kind::Vector: {
            // Triângulos da CPU com a distância até a borda: AA exato no shader.
            if (!vecFrameBuf_.valid() || layer.source.vecCount == 0 || !currentSnap_) return false;
            auto pipe = shaders_.pipeline(PipelineKey::graphics(ShaderId::vector_path_vert, ShaderId::vector_path_frag, kWorkFormat, true,
                                                                BlendMode::Normal));
            if (!pipe.ok()) return false;
            out.texture = graph_.create_texture("layer-vetor", d);
            struct Cap { PipelineHandle p; BufferHandle buf; Mat4 clip; Vec4 params; u32 count; } cap{
                *pipe, vecFrameBuf_, clip_from_comp(static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height)),
                Vec4{static_cast<f32>(currentSnap_->vecBase + layer.source.vecFirst), static_cast<f32>(currentSnap_->vecBase + layer.source.paintFirst), 0, 0},
                layer.source.vecCount};
            graph_.add_raster_pass("vetor", PassStage::Decode, out.texture, LoadOp::Clear, Vec4{0, 0, 0, 0}, [cap](PassContext& pc) {
                pc.cmds.bind_pipeline(cap.p);
                pc.cmds.bind_storage_buffer(cap.buf);
                struct { Mat4 m; Vec4 params; } push{cap.clip, cap.params};
                pc.cmds.push_constants(&push, sizeof(push));
                pc.cmds.draw(cap.count);
            });
            return true;
        }
        case LayerSource::Kind::Particles: {
            out.texture = graph_.create_texture("layer-particulas", d);
            // Desfoque (8.2): K subamostras de tempo somadas com peso 1/K.
            const u32 nsub = layer.particle.flags ? std::max<u32>(1u, static_cast<u32>(layer.particle.subs.size())) : 1u;
            if (layer.particle.flags && !particleFrameBuf_.valid()) return false;
            // 8.2: dados extras da camada (binding AUREA_DATA) — ver particle_extras_bind.
            const ParticleExtrasBind px = particle_extras_bind(layer.source, frameNumber);
            PipelineKey pkey = PipelineKey::graphics(
                ShaderId::particles_particles_vert, ShaderId::particles_particles_frag, kWorkFormat, true,
                layer.source.particleAdditive || nsub > 1 ? BlendMode::Add : BlendMode::Normal);
            // Partícula de MALHA: profundidade de verdade entre as malhas (teste
            // e escrita, Z reverso, limpa em 0) num alvo próprio. Com desfoque
            // (subamostras somadas no mesmo passe) a profundidade de uma
            // subamostra taparia a seguinte: aí vale só a soma, sem depth.
            const bool meshDepth = px.mesh && nsub == 1;
            if (meshDepth) {
                pkey.hasDepth = true;
                pkey.depthTest = true;
                pkey.depthWrite = true;
                pkey.depthCompare = CompareOp::GreaterOrEqual;
                pkey.depthFormat = SurfaceFormat::Depth32F;
                pkey.cull = CullMode::None;
            }
            auto pipe = shaders_.pipeline(pkey);
            if (!pipe.ok()) return false;
            if (!particleQuad_.valid()) {
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
            }
            // O push constant leva a matriz e os slots; os PARAMETROS vao
            // como uniformes (20 vec4 = 320 B, acima do limite garantido de
            // push constant que e 128 B).
            // Push de 112 B (ParticleScene.hpp): sem bits de modo = o 2D de sempre.
            ParticlePush* pushes = arena_.alloc_array<ParticlePush>(nsub);
            if (!pushes) return false;
            for (u32 s = 0; s < nsub; ++s) pushes[s] = particle_push(layer, s, 1.0f / static_cast<f32>(nsub));
            struct Cap {
                PipelineHandle p; ParticlePush* clip; Vec4 params[LayerSource::kParticleBlocks];
                u32 slots; BufferHandle quad; u32 subs; BufferHandle history;
                u32 meshVerts; BufferHandle extras; TextureHandle tex; u64 sampler;   // 8.2 extras
            } cap{*pipe, pushes, {}, layer.source.particleSlots, particleQuad_, nsub,
                  layer.particle.flags ? particleFrameBuf_ : BufferHandle{},
                  px.mesh ? px.meshVertices : 0u, px.buffer, px.texture, shaders_.sampler(CommonSampler::LinearClamp).id};
            for (u32 i = 0; i < LayerSource::kParticleBlocks; ++i) cap.params[i] = layer.source.particleBlock[i];
            particle_extras_params(px, cap.params);
            heavyStats_.lastParticleSlots += layer.source.particleSlots;
            ++heavyStats_.lastParticleLayers;
            auto draw = [cap](PassContext& pc) {
                pc.cmds.bind_pipeline(cap.p);
                pc.cmds.set_uniforms(cap.params, sizeof(cap.params));
                if (cap.history.valid()) pc.cmds.bind_storage_buffer_at(1, cap.history);
                // O binding do storage buffer sobrevive entre passes: liga sempre
                // (inválido = o nulo do backend; o shader não lê sem extras.z).
                pc.cmds.bind_storage_buffer(cap.extras);
                if (cap.tex.valid()) pc.cmds.bind_texture(0, cap.tex, SamplerHandle{cap.sampler});
                if (!cap.meshVerts) pc.cmds.bind_index_buffer(cap.quad, 0, IndexType::U16);
                for (u32 s = 0; s < cap.subs; ++s) {
                    pc.cmds.push_constants(&cap.clip[s], sizeof(ParticlePush));
                    // Malha instanciada: gl_VertexIndex = vértice da malha,
                    // instância = partícula (a mesma conta do quad).
                    if (cap.meshVerts) pc.cmds.draw(cap.meshVerts, cap.slots, 0);
                    else pc.cmds.draw_indexed(6, cap.slots, 0, 0, 0);
                }
            };
            if (meshDepth) {
                TextureDesc dd = d;
                dd.format = SurfaceFormat::Depth32F;
                dd.sampled = false;
                dd.debugName = "particulas-profundidade";
                const FGTexture depth = graph_.create_texture("particulas-profundidade", dd);
                graph_.add_raster_pass_depth("particulas", PassStage::Decode, out.texture, LoadOp::Clear, Vec4{0, 0, 0, 0},
                                             depth, LoadOp::Clear, false, 0.0f, draw);
            } else {
                graph_.add_raster_pass("particulas", PassStage::Decode, out.texture, LoadOp::Clear, Vec4{0, 0, 0, 0}, draw);
            }
            return true;
        }
        case LayerSource::Kind::None: break;
    }
    return false;
}

// =============================================================================
// Aurea Particular 8.2 — dados extras (render/ParticleExtras)
// =============================================================================
void Renderer::prepare_particle_extras(const Composition& comp, const Layer& layer, const ParticleData& pd,
                                       FrameIndex time, const ImagePixels* (*imageLookup)(void*, AssetId),
                                       void* imageCtx, u64 frameNumber, LayerId rid, LayerSource& src) noexcept {
    src.particleExtras.reset();
    // Só liga com algum recurso do 8.2 em uso: sem ele o shader nem lê o
    // buffer e o quadro é exatamente o de antes (projeto antigo não muda).
    const bool sourced = pd.emitterSource != 0 && pd.emitterType >= static_cast<u32>(ParticleEmitter::Layer)
                      && pd.emitterType <= static_cast<u32>(ParticleEmitter::Mesh);
    const bool texture = pd.particleType == static_cast<u32>(ParticleShape::Texture) && pd.textureAsset != 0;
    const bool mesh = pd.particleType == static_cast<u32>(ParticleShape::Mesh) && pd.meshSource != 0;
    const bool look = pd.colorStopCount > 0 || pd.sizeCurveCount > 0 || pd.opacityCurveCount > 0
                   || pd.sizeRandom > 0.0f || pd.opacityRandom > 0.0f || pd.colorRandom > 0.0f
                   || pd.auxProbability < 1.0f || pd.trailWidth != 1.0f || pd.trailOpacity < 1.0f
                   || pd.collision >= static_cast<u32>(ParticleCollision::Sphere);
    if (!sourced && !texture && !mesh && !look) return;
    auto fd = std::make_shared<particles::FrameData>();
    particles::BuildContext ctx;
    ctx.comp = &comp;
    ctx.time = time;
    ctx.imageLookup = imageLookup;
    ctx.imageCtx = imageCtx;
    ctx.modelLookup = modelLookup_;
    ctx.modelCtx = modelCtx_;
    ctx.frameNumber = frameNumber;
    particles::build_frame(ctx, layer, pd, particleStatics_, *fd);
    fd->layerKey = rid.pack();
    // Imagem da partícula: a MESMA RGBA8 (sRGB, alfa reto) das camadas de
    // imagem, no mesmo mapa — uma imagem usada como camada e como partícula
    // sobe uma vez só. O shader converte para linear na hora de amostrar.
    if (fd->texture.valid() && backend_) {
        const ImagePixels* px = imageLookup ? imageLookup(imageCtx, fd->texture) : nullptr;
        const u64 key = fd->texture.pack();
        auto it = images_.find(key);
        if (it != images_.end() && px && (it->second.width != px->width || it->second.height != px->height)) {
            destroy_image_linear(it->second);
            backend_->destroy_texture(it->second.texture);
            images_.erase(it);
            it = images_.end();
        }
        if (it == images_.end() && px && px->width && px->height) {
            TextureDesc d;
            d.width = px->width;
            d.height = px->height;
            d.format = SurfaceFormat::RGBA8;
            d.sampled = true;
            d.transferDst = true;
            d.debugName = "imagem-particula";
            auto tex = backend_->create_texture(d);
            if (tex.ok()) {
                PendingUpload up;
                up.texture = *tex;
                up.bytesPerRow = px->width * 4;
                up.data = px->rgba;
                uploads_.push_back(std::move(up));
                images_[key] = ImageTexture{*tex, px->width, px->height, frameNumber};
            }
        } else if (it != images_.end()) {
            it->second.lastFrame = frameNumber;
        }
    }
    heavyStats_.particleExtraCpuBytes = particleStatics_.resident_bytes();
    src.particleExtras = std::move(fd);
}

void Renderer::particle_extras_params(const ParticleExtrasBind& b, Vec4* params) noexcept {
    // extras (bloco 19): slots do fluxo (já), 1º vec4 do cabeçalho, dados prontos, passe de malha.
    params[19].y = static_cast<f32>(b.headerBase);
    params[19].z = b.on ? 1.0f : 0.0f;
    params[19].w = b.mesh ? 1.0f : 0.0f;
}

Renderer::ParticleExtrasBind Renderer::particle_extras_bind(const LayerSource& src, u64 frameNumber) noexcept {
    ParticleExtrasBind b;
    if (!src.particleExtras || !backend_) return b;
    const particles::FrameData& fd = *src.particleExtras;
    b.buffer = particleExtraBufs_.upload(*backend_, fd, frameNumber, b.headerBase);
    if (!b.buffer.valid()) return b;   // sem buffer: o caminho de antes (nunca lixo)
    b.on = true;
    if (fd.texture.valid()) {
        auto it = images_.find(fd.texture.pack());
        if (it != images_.end()) {
            b.texture = it->second.texture;
            it->second.lastFrame = frameNumber;
        }
    }
    b.mesh = fd.meshMode && fd.meshVertices >= 3;
    b.meshVertices = fd.meshVertices;
    ++heavyStats_.lastParticleExtraLayers;
    heavyStats_.particleExtraGpuBytes = particleExtraBufs_.resident_bytes();
    return b;
}

void Renderer::upload_masks(FrameSnapshot& snap) noexcept {
    // Blocos de todas as pré-composições num buffer só do quadro (como os glifos).
    usize total = 0;
    std::vector<FrameSnapshot*>& all = snapAll_;       // listas do renderer: sem alocar por quadro
    std::vector<FrameSnapshot*>& stack = snapStack_;
    all.clear();
    stack.assign(1, &snap);
    while (!stack.empty()) {
        FrameSnapshot* s = stack.back();
        stack.pop_back();
        s->maskBase = static_cast<u32>(total);
        total += s->maskData.size();
        all.push_back(s);
        for (auto& c : s->nested) if (c) stack.push_back(c.get());
    }
    maskFrameBuf_ = BufferHandle{};
    if (total == 0) return;
    const u32 slot = maskSlot_++ % kGlyphRing;
    const usize bytes = total * sizeof(Vec4);
    if (maskCap_[slot] < bytes) {
        if (maskBuf_[slot].valid()) backend_->destroy_buffer(maskBuf_[slot]);
        BufferDesc bd;
        bd.bytes = std::max<usize>(bytes * 2, 1024 * sizeof(Vec4));
        bd.usage = BufferUsage::Storage;
        bd.access = MemoryAccess::Upload;
        bd.debugName = "mascaras";
        auto b = backend_->create_buffer(bd);
        maskBuf_[slot] = b.ok() ? *b : BufferHandle{};
        maskCap_[slot] = b.ok() ? bd.bytes : 0;
    }
    void* ptr = nullptr;
    if (!maskBuf_[slot].valid() || !backend_->map_buffer(maskBuf_[slot], ptr).ok() || !ptr) { mark_incomplete(__FILE__, __LINE__); return; }
    auto* dst = static_cast<Vec4*>(ptr);
    for (FrameSnapshot* s : all) std::copy(s->maskData.begin(), s->maskData.end(), dst + s->maskBase);
    backend_->unmap_buffer(maskBuf_[slot]);
    maskFrameBuf_ = maskBuf_[slot];
}

void Renderer::apply_masks(const RenderLayer& layer, LayerImage& img, u64 frameNumber) noexcept {
    if (layer.maskCount == 0 || !img.valid() || !currentSnap_) return;
    const u32 w = img.width, h = img.height;
    const f32 k = img.region.w > 0.0f ? static_cast<f32>(w) / img.region.w : 1.0f;
    // A cobertura depende do bloco (caminho, modos, feather...), do tamanho e
    // da região: com tudo igual, a textura do quadro anterior serve.
    u64 key = layer.maskKey ^ (static_cast<u64>(w) << 40) ^ (static_cast<u64>(h) << 20);
    for (f32 v : {img.region.x, img.region.y, img.region.w, img.region.h}) {
        u32 b;
        std::memcpy(&b, &v, 4);
        key = (key ^ b) * 1099511628211ull;
    }
    key |= 1ull;
    TextureDesc md;
    md.width = w;
    md.height = h;
    md.format = SurfaceFormat::R8;
    md.sampled = true;
    md.renderTarget = true;
    md.debugName = "cobertura-da-mascara";
    MaskCache& mc = maskCache_[layer.id.pack()];
    mc.lastFrame = frameNumber;
    if (mc.width != w || mc.height != h) {
        for (TextureHandle& t : mc.tex) if (t.valid()) backend_->destroy_texture(t);
        mc = MaskCache{};
        mc.width = w;
        mc.height = h;
        mc.lastFrame = frameNumber;
    }
    FGTexture cov{};
    for (u32 i = 0; i < 2; ++i) {
        if (mc.tex[i].valid() && mc.key[i] == key) {
            ++maskHits_;
            ++heavyStats_.maskCacheHits;
            cov = graph_.import_texture("cobertura-da-mascara", mc.tex[i], md);
            break;
        }
    }
    if (!cov.valid()) {
        if (!maskFrameBuf_.valid()) return;
        auto pipe = shaders_.pipeline(PipelineKey::fullscreen(ShaderId::mask_raster_frag, SurfaceFormat::R8));
        if (!pipe.ok()) { mark_incomplete(__FILE__, __LINE__); return; }
        // A que um quadro em voo pode estar lendo nunca é a que se escreve.
        const u32 slot = mc.next;
        mc.next ^= 1u;
        if (!mc.tex[slot].valid()) {
            auto t = backend_->create_texture(md);
            if (!t.ok()) return;
            mc.tex[slot] = *t;
        }
        mc.key[slot] = key;
        ++maskMisses_;
        ++heavyStats_.maskRasterizations;
        cov = graph_.import_texture("cobertura-da-mascara", mc.tex[slot], md);
        struct { Vec4 region; Vec4 info; } u{Vec4{img.region.x, img.region.y, img.region.w, img.region.h},
                                             Vec4{static_cast<f32>(currentSnap_->maskBase + layer.maskFirst),
                                                  static_cast<f32>(layer.maskCount), k, layer.maskStart}};
        void* ubo = arena_.alloc(sizeof(u), 16);
        std::memcpy(ubo, &u, sizeof(u));
        struct Cap { PipelineHandle p; BufferHandle buf; void* ubo; } cap{*pipe, maskFrameBuf_, ubo};
        graph_.add_raster_pass("mascara-raster", PassStage::Mask, cov, LoadOp::DontCare, Vec4{0, 0, 0, 0},
                               [cap](PassContext& pc) {
            pc.cmds.bind_pipeline(cap.p);
            pc.cmds.bind_storage_buffer(cap.buf);
            pc.cmds.set_uniforms(cap.ubo, 32);
            pc.cmds.draw(3);
        });
    }
    TextureDesc od;
    od.width = w;
    od.height = h;
    od.format = kWorkFormat;
    od.sampled = true;
    od.renderTarget = true;
    const FGTexture masked = graph_.create_texture("layer-mascarada", od);
    EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat, 8192);
    if (ctx.fullscreen_pass("mascara", PassStage::Mask, masked, ShaderId::mask_apply_frag,
                            {PassTexture{img.texture}, PassTexture{cov}}, nullptr, 0) != kInvalidIndex) {
        img.texture = masked;
    }
}

FGTexture Renderer::draw_to_comp(const CompositeDraw& d, const TextureDesc& compDesc, f32 compW, f32 compH,
                                 const char* name) noexcept {
    auto pNormal = shaders_.pipeline(PipelineKey::graphics(
        ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Normal));
    if (!pNormal.ok()) { mark_incomplete(__FILE__, __LINE__); return FGTexture{}; }
    TextureDesc td = compDesc;
    td.transferSrc = false;
    const FGTexture t = graph_.create_texture(name, td);
    struct Cap { CompositeDraw d; PipelineHandle p; f32 w, h; } cap{d, *pNormal, compW, compH};
    const u32 pass = graph_.add_raster_pass(name, PassStage::Composite, t, LoadOp::Clear, Vec4{0, 0, 0, 0},
                                            [cap](PassContext& pc) {
        pc.cmds.bind_pipeline(cap.p);
        LayerPush push;
        push.clipFromLayer = clip_from_comp(cap.w, cap.h) * cap.d.compFromLayer;
        push.region = Vec4{cap.d.region.x, cap.d.region.y, cap.d.region.w, cap.d.region.h};
        push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
        push.params = Vec4{cap.d.opacity, 0.0f, 0.0f, 0.0f};
        pc.cmds.bind_texture(0, pc.texture(cap.d.texture), SamplerHandle{cap.d.sampler});
        pc.cmds.push_constants(&push, sizeof(push));
        pc.cmds.draw(6);
    });
    graph_.read(pass, d.texture);
    return t;
}

FGTexture Renderer::adjust_full_frame(const LayerImage& fin, const Mat4& fold, f32 opacity, Vec4 under,
                                      const TextureDesc& compDesc, f32 compW, f32 compH) noexcept {
    auto pNormal = shaders_.pipeline(PipelineKey::graphics(
        ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Normal));
    if (!pNormal.ok() || !fin.valid()) { mark_incomplete(__FILE__, __LINE__); return FGTexture{}; }
    TextureDesc td = compDesc;
    td.transferSrc = false;
    td.sampled = true;
    td.renderTarget = true;
    const FGTexture t = graph_.create_texture("ajuste-quadro", td);
    struct Cap { PipelineHandle p; FGTexture src; u64 sampler; Mat4 m; Vec4 region; f32 w, h, opacity; } cap{
        *pNormal, fin.texture, shaders_.sampler(CommonSampler::LinearBorder).id, fold,
        Vec4{fin.region.x, fin.region.y, fin.region.w, fin.region.h}, compW, compH, std::clamp(opacity, 0.0f, 1.0f)};
    const u32 pass = graph_.add_raster_pass("ajuste-quadro", PassStage::Composite, t, LoadOp::Clear, under,
                                            [cap](PassContext& pc) {
        pc.cmds.bind_pipeline(cap.p);
        LayerPush push;
        push.clipFromLayer = clip_from_comp(cap.w, cap.h) * cap.m;
        push.region = cap.region;
        push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
        push.params = Vec4{cap.opacity, 0.0f, 0.0f, 0.0f};
        pc.cmds.bind_texture(0, pc.texture(cap.src), SamplerHandle{cap.sampler});
        pc.cmds.push_constants(&push, sizeof(push));
        pc.cmds.draw(6);
    });
    graph_.read(pass, fin.texture);
    return t;
}

void Renderer::adjust_time_input(const FrameSnapshot& snap, const RenderLayer& layer, u32 target, LayerImage& in,
                                 const TextureDesc& compDesc, u64 frameNumber) noexcept {
    if (target >= layer.adjustTime.size() || !in.valid()) return;
    const RenderLayer::AdjustTime& at = layer.adjustTime[target];
    if (!at.active()) return;
    // Um fundo re-renderizado (pré-composição da mesma composição, no mesmo
    // tamanho do alvo). Faltou (orçamento, recurso a caminho): o de agora.
    auto backdrop = [&](u32 index, FGTexture present) -> FGTexture {
        if (index == ~0u) return present;
        const FrameSnapshot* s = index < snap.nested.size() ? snap.nested[index].get() : nullptr;
        if (!s || !s->target.valid()) { mark_incomplete(__FILE__, __LINE__); return present; }
        const TextureDesc& d = graph_.desc(s->target);
        if (d.width != in.width || d.height != in.height) { mark_incomplete(__FILE__, __LINE__); return present; }
        return s->target;
    };
    const u32 maxTex = std::min<u32>(backend_->capabilities().maxTexture2D, 8192);
    EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat, maxTex);
    TextureDesc d = compDesc;
    d.transferSrc = false;
    d.sampled = true;
    d.renderTarget = true;
    d.width = in.width;
    d.height = in.height;
    const f32 w = static_cast<f32>(in.width), h = static_cast<f32>(in.height);
    auto pNormal = shaders_.pipeline(PipelineKey::graphics(
        ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Normal));
    auto pAdd = shaders_.pipeline(PipelineKey::graphics(
        ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Add));
    // 0. Sem a cor de fundo (efeitos que leem o alfa da pilha).
    if (at.bare != ~0u) in.texture = backdrop(at.bare, in.texture);
    // 1. O instante mostrado (Posterizar, Low FPS, Remapear tempo, Flow Retime).
    const FGTexture present = in.texture;
    if (at.now != ~0u) in.texture = backdrop(at.now, present);
    // 1b. O quadro seguinte: mistura, movimento de pixels, desfoque vetorial.
    if (at.next != ~0u) {
        const FGTexture b = backdrop(at.next, FGTexture{});
        if (b.valid()) {
            FGTexture flow{};
            u32 baseW = 0, baseH = 0;
            if ((at.blendMode == 2 && at.blendT > 0.0f) || at.vectorBlur > 0.0f) {
                // O fundo muda com qualquer edição: o fluxo é deste quadro
                // (a chave leva o número do quadro; nada velho sai do cache).
                u64 pair = frameNumber * 0x9E3779B97F4A7C15ull ^ (static_cast<u64>(at.now) << 32) ^ at.next;
                pair ^= static_cast<u64>(std::lround(at.flowBase)) << 20;
                const u64 key = layer.id.pack() ^ (0xAD7ull << 52) ^ (static_cast<u64>(target) << 44);
                flow = video_flow(key, pair, in.texture, b, in.width, in.height, at.flowBase, baseW, baseH, frameNumber);
                if (!flow.valid()) mark_incomplete(__FILE__, __LINE__);
            }
            bool warped = false;
            if (at.blendT > 0.0f && at.blendMode == 2 && flow.valid()) {
                const FGTexture moved = graph_.create_texture("ajuste-movimento", d);
                const Vec4 wp{at.blendT, 1.0f / static_cast<f32>(baseW), 1.0f / static_cast<f32>(baseH), 0};
                if (ctx.fullscreen_pass("ajuste-flow-deforma", PassStage::Effects, moved, ShaderId::video_flow_warp_frag,
                                        {PassTexture{in.texture}, PassTexture{b}, PassTexture{flow}}, &wp, sizeof(wp)) != kInvalidIndex) {
                    in.texture = moved;
                    warped = true;
                }
            }
            if (!warped && at.blendT > 0.0f && at.blendMode >= 1 && pNormal.ok()) {
                const FGTexture mix = graph_.create_texture("ajuste-mistura", d);
                struct Cap { PipelineHandle p; FGTexture a; FGTexture b; u64 sampler; f32 w; f32 h; f32 t; }
                    cap{*pNormal, in.texture, b, shaders_.sampler(CommonSampler::LinearClamp).id, w, h, at.blendT};
                const u32 pass = graph_.add_raster_pass("ajuste-mistura", PassStage::Effects, mix, LoadOp::Clear,
                                                        Vec4{0, 0, 0, 0}, [cap](PassContext& pc) {
                    pc.cmds.bind_pipeline(cap.p);
                    LayerPush push;
                    push.clipFromLayer = clip_from_comp(cap.w, cap.h);
                    push.region = Vec4{0.0f, 0.0f, cap.w, cap.h};
                    push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
                    pc.cmds.bind_texture(0, pc.texture(cap.a), SamplerHandle{cap.sampler});
                    push.params = Vec4{1.0f, 0, 0, 0};
                    pc.cmds.push_constants(&push, sizeof(push));
                    pc.cmds.draw(6);
                    pc.cmds.bind_texture(0, pc.texture(cap.b), SamplerHandle{cap.sampler});
                    push.params = Vec4{cap.t, 0, 0, 0};
                    pc.cmds.push_constants(&push, sizeof(push));
                    pc.cmds.draw(6);
                });
                graph_.read(pass, in.texture);
                graph_.read(pass, b);
                in.texture = mix;
            }
            if (at.vectorBlur > 0.0f && flow.valid()) {
                const FGTexture blurred = graph_.create_texture("ajuste-desfoque-vetorial", d);
                const Vec4 vp{at.vectorBlur, 1.0f / static_cast<f32>(baseW), 1.0f / static_cast<f32>(baseH),
                              static_cast<f32>(std::clamp<u32>(heavyQ_.flowBlurSamples, 8u, 64u))};
                if (ctx.fullscreen_pass("ajuste-desfoque-vetorial", PassStage::Effects, blurred, ShaderId::video_flow_vblur_frag,
                                        {PassTexture{in.texture}, PassTexture{flow}}, &vp, sizeof(vp)) != kInvalidIndex)
                    in.texture = blurred;
                else mark_incomplete(__FILE__, __LINE__);
            }
        }
    }
    // 2. RGB no tempo: um canal de cada instante.
    if (at.rgb) {
        const FGTexture chan[3] = {backdrop(at.channel[0], in.texture), backdrop(at.channel[1], in.texture),
                                   backdrop(at.channel[2], in.texture)};
        const FGTexture merged = graph_.create_texture("ajuste-rgb-no-tempo", d);
        if (ctx.fullscreen_pass("ajuste-rgb-no-tempo", PassStage::Effects, merged, ShaderId::effects_rgb_merge_frag,
                                {PassTexture{chan[0], {}, CommonSampler::LinearClamp},
                                 PassTexture{chan[1], {}, CommonSampler::LinearClamp},
                                 PassTexture{chan[2], {}, CommonSampler::LinearClamp}}, nullptr, 0) != kInvalidIndex)
            in.texture = merged;
        else mark_incomplete(__FILE__, __LINE__);
    }
    // 3. Eco: as cópias atrasadas somadas com o peso (e o canal do atraso RGB),
    //    como o eco de uma pré-composição.
    if (!at.echo.empty() && pAdd.ok()) {
        const u32 k = static_cast<u32>(at.echo.size());
        FGTexture* sources = arena_.alloc_array<FGTexture>(k);
        Vec4* params = arena_.alloc_array<Vec4>(k);
        if (sources && params) {
            for (u32 s = 0; s < k; ++s) {
                sources[s] = backdrop(at.echo[s].nested, in.texture);
                params[s] = Vec4{at.echo[s].weight, at.echo[s].mask.x, at.echo[s].mask.y, at.echo[s].mask.z};
            }
            const FGTexture acc = graph_.create_texture("ajuste-eco", d);
            struct Cap { PipelineHandle p; const FGTexture* src; const Vec4* params; u32 k; u64 sampler; f32 w, h; }
                cap{*pAdd, sources, params, k, shaders_.sampler(CommonSampler::LinearClamp).id, w, h};
            const u32 pass = graph_.add_raster_pass("ajuste-eco", PassStage::Effects, acc, LoadOp::Clear, Vec4{0, 0, 0, 0},
                                                    [cap](PassContext& pc) {
                pc.cmds.bind_pipeline(cap.p);
                LayerPush push;
                push.clipFromLayer = clip_from_comp(cap.w, cap.h);
                push.region = Vec4{0.0f, 0.0f, cap.w, cap.h};
                push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
                for (u32 s = 0; s < cap.k; ++s) {
                    pc.cmds.bind_texture(0, pc.texture(cap.src[s]), SamplerHandle{cap.sampler});
                    push.params = cap.params[s];
                    pc.cmds.push_constants(&push, sizeof(push));
                    pc.cmds.draw(6);
                }
            });
            for (u32 s = 0; s < k; ++s) {
                bool seen = false;
                for (u32 q = 0; q < s; ++q) seen |= sources[q] == sources[s];
                if (!seen) graph_.read(pass, sources[s]);
            }
            in.texture = acc;
        }
    }
    // 4. O passado (Detectar movimento, Datamosh, Glitch de vídeo): o
    //    EffectGraph o leva pelas etapas anteriores até o efeito que pede.
    if (at.hasHistory) in.history = backdrop(at.history, present);
    // 5. Aprimorar: a composição abaixo nos quadros vizinhos.
    in.neighbors = TemporalNeighbors{};
    for (const RenderLayer::NeighborSample& n : at.neighbors) {
        if (in.neighbors.count >= kMaxTemporalNeighbors) break;
        const FGTexture t = backdrop(n.nested, FGTexture{});
        if (!t.valid()) continue;
        in.neighbors.texture[in.neighbors.count] = t;
        in.neighbors.offset[in.neighbors.count] = static_cast<i8>(n.offset);
        ++in.neighbors.count;
    }
}

void Renderer::compose_layers(FrameSnapshot& snap, FGTexture comp, const TextureDesc& compDesc, u64 frameNumber,
                              std::vector<CompositeDraw>& draws, u32 depth) noexcept {
    // Pré-composições primeiro: cada uma no seu alvo, na MESMA escala de
    // prévia deste (tamanho da filha × alvo/composição).
    for (std::unique_ptr<FrameSnapshot>& child : snap.nested) {
        if (!child || depth >= 8) continue;
        const f32 kx = static_cast<f32>(compDesc.width) / static_cast<f32>(std::max(1u, snap.compWidth));
        const f32 ky = static_cast<f32>(compDesc.height) / static_cast<f32>(std::max(1u, snap.compHeight));
        TextureDesc d = compDesc;
        d.transferSrc = false;
        d.width = std::max(1u, static_cast<u32>(std::lround(static_cast<f32>(child->compWidth) * kx)));
        d.height = std::max(1u, static_cast<u32>(std::lround(static_cast<f32>(child->compHeight) * ky)));
        child->target = graph_.create_texture("pre-composicao", d);
        std::vector<CompositeDraw> childDraws;
        compose_layers(*child, child->target, d, frameNumber, childDraws, depth + 1);
    }
    // Estado da composição em curso (a recursão acima trocou).
    currentScenes_ = &snap.scenes;
    currentSnap_ = &snap;
    compTargetW_ = compDesc.width;
    compTargetH_ = compDesc.height;

    // --- Layers: fonte → máscaras → efeitos → desenho na composição.
    EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat,
                           std::min<u32>(backend_->capabilities().maxTexture2D, 8192));
    // Camadas 2D que vivem numa cena 3D: imagem (com efeitos) primeiro; o
    // grupo as desenha com profundidade junto dos modelos.
    groupPlanes_.assign(snap.scenes.size(), {});
    for (u32 i = 0; i < snap.layers.size(); ++i) {
        const RenderLayer& layer = snap.layers[i];
        if (layer.planeGroup < 0 || static_cast<usize>(layer.planeGroup) >= groupPlanes_.size()) continue;
        LayerImage src;
        const bool hasEffects = i < snap.plans.size() && !snap.plans[i].empty();
        if (!build_source(layer, i, hasEffects || layer.maskCount > 0, src, framesInFlight_, frameNumber)) {
            mark_incomplete(__FILE__, __LINE__);
            continue;
        }
        apply_masks(layer, src, frameNumber);
        LayerImage fin = src;
        motion_tile::PeriodicPlane periodic;
        raster_extrude::Options extrusionOptions;
        // 3D Shatter num plano da cena: o efeito devolve a camada intacta e o
        // grupo desenha as peças como geometria (scene3d/shatter.vert).
        const EffectEval* shatterItem=nullptr;
        if (i<snap.plans.size()) for (const EffectEval& item:snap.plans[i].evals) {
            EffectEval eval=item;eval.values=snap.plans[i].values.data()+item.valueOffset;
            const auto options=raster_extrude::options_from(eval);
            if(options.depth>0)extrusionOptions=options;
            if(shatter3d::is_shatter(item.effect->type_id()))shatterItem=&item;
        }
        bool periodicReady = false;
        if (hasEffects) {
            const auto& plan = snap.plans[i];
            const auto& last = plan.stages.back();
            // Only the terminal domain operation can move to plane sampling.
            // Later effects and a folded downstream transform retain the
            // established raster graph; its sampling region remains meaningful.
            const bool terminalTile = !plan.hasFold && last.kind == EffectStage::Kind::Single && last.count == 1
                && plan.evals[last.begin].effect->type_id() == effect_type_id(effect_keys::kMotionTile);
            if (terminalTile && !(extrusionOptions.depth>0)) {
                EffectPlan prefix = plan;
                prefix.stages.pop_back();
                (void)EffectGraph::build(prefix, ctx, src, fin);
                EffectEval eval = plan.evals[last.begin];
                eval.values = plan.values.data() + eval.valueOffset;
                eval.placement = &plan.placement;
                periodicReady = motion_tile::build_scene_plane(ctx, eval, fin, periodic);
            }
            if (!periodicReady) {
                const Status built=EffectGraph::build(plan,ctx,src,fin);
                if(extrusionOptions.depth>0 && !built.ok()){incomplete_=true;continue;}
            }
        }
        scene3d::ScenePlane p;
        p.texture = periodicReady ? periodic.textures[0] : fin.texture;
        if (periodicReady) p.periodic = periodic;
        // Sólido otimizado (1×1): clamp, como na composição (a borda transparente
        // viraria um degradê).
        p.sampler = shaders_.sampler(periodicReady
            ? (periodic.flags.x > .5f ? CommonSampler::LinearClamp : CommonSampler::LinearRepeat)
            : (layer.source.kind == LayerSource::Kind::Solid && fin.width == 1)
                ? CommonSampler::LinearClamp : CommonSampler::LinearBorder).id;
        p.clipFromLayer = clip_from_comp(static_cast<f32>(snap.compWidth), static_cast<f32>(snap.compHeight)) * layer.compFromLayer;
        p.region = Vec4{fin.region.x, fin.region.y, fin.region.w, fin.region.h};
        p.opacity = layer.opacity;
        if (extrusionOptions.depth>0) {
            const auto& plan=snap.plans[i];
            const Mat4 fold=plan.hasFold?plan.foldMatrix:Mat4::identity();
            p.clipFromLayer=p.clipFromLayer*fold;
            p.opacity*=plan.hasFold?plan.foldOpacity:1.f;
            if (!raster_extrude::build_volume(ctx,fin,extrusionOptions,p.clipFromLayer,layer.worldFromLayer*fold,p.extrusion)) {
                incomplete_=true;continue;
            }
            if(!p.extrusion.count)continue;
        }
        if (shatterItem && !p.extrusion.count && !periodicReady) {
            EffectEval eval=*shatterItem;
            eval.values=snap.plans[i].values.data()+eval.valueOffset;
            eval.placement=&snap.plans[i].placement;
            p.shatter.pieces=shatter3d::fill(eval,fin,p.shatter.uniforms);
        }
        const Vec4 c = p.clipFromLayer * Vec4{fin.region.x + fin.region.w * 0.5f, fin.region.y + fin.region.h * 0.5f, p.extrusion.count?p.extrusion.params.x*.5f:0.f, 1};
        p.viewDepth = c.w;
        p.sourceLayer = i;
        // Luzes da composição (o bloco do grupo) no mundo desta camada.
        if (static_cast<usize>(layer.planeGroup) < snap.scenes.size()) p.light = snap.scenes[static_cast<usize>(layer.planeGroup)].planeLight;
        p.light.worldFromLayer = layer.worldFromLayer;
        // "Aceita luzes" desligado (o padrão, e todo projeto anterior à v45):
        // o plano sai como no 2D mesmo com luz na composição — as luzes de
        // um projeto do 2138 eram para os modelos e não podem lavar as imagens.
        if (!layer.acceptsLights) p.light.cameraPos.w = 0.0f;
        groupPlanes_[static_cast<usize>(layer.planeGroup)].push_back(p);
    }
    const f32 compW = static_cast<f32>(snap.compWidth), compH = static_cast<f32>(snap.compHeight);
    // Uma camada até o desenho na composição (fonte, máscaras, efeitos,
    // desfoque/eco acumulados). false = fora deste quadro.
    auto make_draw = [&](u32 i, CompositeDraw& draw) -> bool {
        const RenderLayer& layer = snap.layers[i];
        LayerImage src;
        const bool hasEffects = i < snap.plans.size() && !snap.plans[i].empty();
        if (layer.source.kind == LayerSource::Kind::Text && layer.source.glyphSets > 1 && layer.temporal.empty()) {
            // Sample glyph animation and layer motion together. Blurring text
            // locally first and its layer second convolves unrelated times.
            const u32 count = layer.source.glyphSets;
            auto pipeline = shaders_.pipeline(PipelineKey::graphics(
                ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Add));
            if (!pipeline.ok()) { mark_incomplete(__FILE__, __LINE__); return false; }
            FGTexture integrated{};
            MotionBlurBounds bounds{Rect{0,0,compW,compH},compDesc.width,compDesc.height};
            const EffectPlan* plan = i < snap.plans.size() ? &snap.plans[i] : nullptr;
            for (u32 s = 0; s < count; ++s) {
                LayerImage sample;
                if (!build_source(layer, i, hasEffects || layer.maskCount > 0, sample, framesInFlight_, frameNumber, s)) {
                    mark_incomplete(__FILE__, __LINE__); return false;
                }
                apply_masks(layer, sample, frameNumber);
                LayerImage final = sample;
                if (hasEffects && !EffectGraph::build(*plan, ctx, sample, final).ok()) { mark_incomplete(__FILE__, __LINE__); return false; }
                if (s == 0) {
                    // Every glyph set has the same layout and effect plan. Its
                    // final region bounds all its pixels, including padding,
                    // masks and effect expansion; only the glyph pixels vary.
                    if (motionBlurCropEnabled_) {
                        const Mat4 fold = plan && plan->hasFold && !layer.blurIncludesFold ? plan->foldMatrix : Mat4::identity();
                        const std::span<const Mat4> matrices = layer.blurMatrices.empty()
                            ? std::span<const Mat4>{&layer.compFromLayer,1} : std::span<const Mat4>{layer.blurMatrices};
                        bounds = motion_blur_bounds(final.region,matrices,fold,snap.compWidth,snap.compHeight,
                            compDesc.width,compDesc.height);
                    }
                    TextureDesc desc = compDesc; desc.transferSrc = false;
                    desc.width = bounds.width; desc.height = bounds.height;
                    integrated = graph_.create_texture("texto-exposicao",desc);
                }
                Mat4 matrix = s < layer.blurMatrices.size() ? layer.blurMatrices[s] : layer.compFromLayer;
                if (plan && plan->hasFold && !layer.blurIncludesFold) matrix = matrix * plan->foldMatrix;
                struct TextSample { FGTexture texture; PipelineHandle pipeline; u64 sampler; Mat4 matrix; Vec4 region; f32 weight; } cap{
                    final.texture, *pipeline, shaders_.sampler(CommonSampler::LinearBorder).id,
                    clip_from_comp(bounds.region.w,bounds.region.h)
                        * Mat4::translation(Vec3{-bounds.region.x,-bounds.region.y,0}) * matrix,
                    Vec4{final.region.x, final.region.y, final.region.w, final.region.h}, 1.f / count};
                const u32 pass = graph_.add_raster_pass("texto-exposicao", PassStage::Composite, integrated,
                    s == 0 ? LoadOp::Clear : LoadOp::Load, Vec4{}, [cap](PassContext& pc) {
                        pc.cmds.bind_pipeline(cap.pipeline);
                        pc.cmds.bind_texture(0, pc.texture(cap.texture), SamplerHandle{cap.sampler});
                        LayerPush push;
                        push.clipFromLayer = cap.matrix; push.region = cap.region;
                        push.uvRect = Vec4{0,0,1,1}; push.params = Vec4{cap.weight,0,0,0};
                        pc.cmds.push_constants(&push, sizeof(push)); pc.cmds.draw(6);
                    });
                graph_.read(pass, final.texture);
            }
            draw.texture = integrated; draw.region = bounds.region;
            draw.compFromLayer = Mat4::identity(); draw.blend = layer.blend;
            draw.opacity = layer.opacity * (plan && plan->hasFold ? plan->foldOpacity : 1.f);
            draw.sampler = shaders_.sampler(CommonSampler::LinearClamp).id;
            return true;
        }
        if (!build_source(layer, i, hasEffects || layer.maskCount > 0, src, framesInFlight_, frameNumber)) {
            // Camada fora deste quadro por recurso ainda não pronto (pipeline
            // compilando, textura a caminho): o próximo quadro tenta de novo.
            mark_incomplete(__FILE__, __LINE__);
            return false;
        }
        const LayerImage raw = src;   // antes das máscaras: base das amostras do eco numa pré-composição
        // Máscaras ANTES dos efeitos (como no AE): o blur vê a borda recortada.
        apply_masks(layer, src, frameNumber);
        LayerImage fin = src;
        if (hasEffects) (void)EffectGraph::build(snap.plans[i], ctx, src, fin);
        draw.texture = fin.texture;
        draw.region = fin.region;
        draw.compFromLayer = layer.compFromLayer;
        draw.opacity = layer.opacity;
        draw.blend = layer.blend;
        draw.sampler = shaders_.sampler(layer.source.kind == LayerSource::Kind::Solid && fin.width == 1
                                        ? CommonSampler::LinearClamp : CommonSampler::LinearBorder).id;
        if (i < snap.plans.size() && snap.plans[i].hasFold) {
            draw.compFromLayer = draw.compFromLayer * snap.plans[i].foldMatrix;
            draw.opacity *= snap.plans[i].foldOpacity;
        }
        // TIMESLICE: uma faixa por amostra, cada uma com a imagem do SEU
        // instante (máscaras e efeitos da camada aplicados a cada uma),
        // recortada no shader (composite/time_slice.frag) e somada num alvo do
        // tamanho da composição. Sem interpolar, cada pixel recebe uma amostra
        // de peso 1; com interpolar, duas cujos pesos somam 1.
        if (layer.per_sample_content()
            && (layer.source.kind == LayerSource::Kind::Video || layer.source.kind == LayerSource::Kind::Nested)) {
            // Tempo amplo: a mesma busca por instante, desenhada inteira
            // (composite_layer.frag com o peso) em vez de recortada na faixa.
            const bool wide = layer.temporal.front().slice.w <= 0.0f;
            auto pSlice = shaders_.pipeline(PipelineKey::graphics(ShaderId::composite_layer_vert,
                wide ? ShaderId::composite_layer_frag : ShaderId::composite_time_slice_frag, kWorkFormat, true, BlendMode::Add));
            const u32 k = static_cast<u32>(layer.temporal.size());
            FGTexture* perSample = pSlice.ok() ? arena_.alloc_array<FGTexture>(k) : nullptr;
            if (!perSample) { mark_incomplete(__FILE__, __LINE__); return true; }
            const bool video = layer.source.kind == LayerSource::Kind::Video;
            const u64 sliceKey = layer.id.pack();
            const u32 maxTexSlice = std::min<u32>(backend_->capabilities().maxTexture2D, 8192);
            u32 sw = std::max(1u, static_cast<u32>(std::ceil(static_cast<f32>(layer.source.width) * layer.texelScale)));
            u32 sh = std::max(1u, static_cast<u32>(std::ceil(static_cast<f32>(layer.source.height) * layer.texelScale)));
            if (sw > maxTexSlice || sh > maxTexSlice) {
                const f32 r = static_cast<f32>(maxTexSlice) / static_cast<f32>(std::max(sw, sh));
                sw = std::max(1u, static_cast<u32>(static_cast<f32>(sw) * r));
                sh = std::max(1u, static_cast<u32>(static_cast<f32>(sh) * r));
            }
            TextureDesc sd;
            sd.width = sw; sd.height = sh; sd.format = kWorkFormat;
            sd.sampled = true; sd.renderTarget = true; sd.debugName = "timeslice-faixa";
            for (u32 s = 0; s < k; ++s) {
                perSample[s] = draw.texture;
                const RenderLayer::TemporalSample& sample = layer.temporal[s];
                FGTexture image{};
                if (video) {
                    bool convert = false;
                    if (sample.frame && sample.sourceUs >= 0) {
                        // Quadro exato novo: vira textura persistente da faixa.
                        std::lock_guard<std::mutex> sl(sliceMutex_);
                        SliceTexture* entry = nullptr;
                        for (SliceTexture& e : sliceCache_) if (e.layer == sliceKey && e.us == sample.sourceUs) entry = &e;
                        if (entry && (entry->w != sw || entry->h != sh)) {
                            backend_->destroy_texture(entry->tex);
                            *entry = SliceTexture{};
                            entry->layer = sliceKey; entry->us = sample.sourceUs;
                        }
                        if (!entry) {
                            sliceCache_.push_back(SliceTexture{});
                            entry = &sliceCache_.back();
                            entry->layer = sliceKey; entry->us = sample.sourceUs;
                        }
                        if (!entry->tex.valid()) {
                            auto t = backend_->create_texture(sd);
                            if (t.ok()) { entry->tex = *t; entry->w = sw; entry->h = sh; }
                        }
                        if (entry->tex.valid()) {
                            entry->lastFrame = frameNumber;
                            if (entry->fgFrame == frameNumber) image = entry->fg;   // mesma faixa duas vezes
                            else {
                                image = graph_.import_texture("timeslice-faixa", entry->tex, sd);
                                entry->fg = image; entry->fgFrame = frameNumber;
                                entry->created = frameNumber;
                                sliceGraphFrame_ = frameNumber;
                                convert = true;
                            }
                        }
                    } else if (sample.frame) {
                        // Aproximado (prévia): transitório, nunca guardado. O
                        // quadro não entra no cache da prévia como pronto e,
                        // parado, fica incompleto (finish_render).
                        image = graph_.create_texture("timeslice-aproximada", sd);
                        convert = true;
                        sliceApprox_ = true;
                    } else {
                        // Já convertida antes: só importa.
                        bool resample = false;
                        {
                            std::lock_guard<std::mutex> sl(sliceMutex_);
                            for (SliceTexture& e : sliceCache_) {
                                if (e.layer != sliceKey || e.us != sample.sourceUs || !e.tex.valid()) continue;
                                e.lastFrame = frameNumber;
                                if (e.fgFrame != frameNumber) {
                                    TextureDesc ed = sd;
                                    ed.width = e.w; ed.height = e.h;
                                    e.fg = graph_.import_texture("timeslice-faixa", e.tex, ed);
                                    e.fgFrame = frameNumber;
                                }
                                image = e.fg;
                                resample = e.w != sw || e.h != sh;
                                break;
                            }
                        }
                        if (resample) {
                            // Densidade mudou (AUTO, refino parado, zoom): a faixa
                            // guardada é o instante CERTO noutra resolução. Destruir
                            // e pedir ao decoder fazia o refino e o render seguinte
                            // (na densidade de antes) desfazerem um ao outro sem
                            // fim; reamostrar para o tamanho pedido basta.
                            const FGTexture stored = image;
                            const Vec4 copyMap{1.0f, 1.0f, 0.0f, 0.0f};
                            image = graph_.create_texture("timeslice-reamostrada", sd);
                            if (ctx.fullscreen_pass("timeslice-reamostrada", PassStage::Decode, image, ShaderId::common_copy_frag,
                                    {PassTexture{stored, {}, CommonSampler::LinearClamp}}, &copyMap, sizeof(copyMap)) == kInvalidIndex)
                                image = FGTexture{};
                        }
                    }
                    if (convert) {
                        sliceSalt_ = 0x511CE00000ull + s;
                        const bool built = build_video_source(layer, i, sw, sh, image, frameNumber, sample.frame.get());
                        sliceSalt_ = 0;
                        if (!built) { image = FGTexture{}; }
                        else if (sample.frame->hardwareBuffer) framesInFlight_.push_back(sample.frame);
                    }
                    if (!image.valid()) { mark_incomplete(__FILE__, __LINE__); continue; }
                } else {
                    const u32 n = sample.nested;
                    if (n == ~0u || n == layer.source.nestedIndex) continue;
                    const FrameSnapshot* other = currentSnap_ && n < currentSnap_->nested.size() ? currentSnap_->nested[n].get() : nullptr;
                    if (!other || !other->target.valid()) { mark_incomplete(__FILE__, __LINE__); continue; }
                    image = other->target;
                }
                LayerImage sampleImg = raw;
                sampleImg.texture = image;
                // Os vizinhos do Aprimorar são do quadro de agora, não deste instante.
                sampleImg.neighbors = TemporalNeighbors{};
                if (video) { sampleImg.width = sw; sampleImg.height = sh; }
                apply_masks(layer, sampleImg, frameNumber);
                LayerImage sampleFin = sampleImg;
                if (hasEffects) (void)EffectGraph::build(snap.plans[i], ctx, sampleImg, sampleFin);
                perSample[s] = sampleFin.texture;
            }
            // Orçamento das faixas guardadas: 384 MB; saem primeiro as que este
            // quadro não usou (refeitas pelo decoder se voltarem a ser pedidas).
            {
                std::lock_guard<std::mutex> sl(sliceMutex_);
                u64 bytes = 0;
                for (const SliceTexture& e : sliceCache_) if (e.tex.valid()) bytes += static_cast<u64>(e.w) * e.h * 8u;
                while (bytes > (384ull << 20)) {
                    auto oldest = sliceCache_.end();
                    for (auto it = sliceCache_.begin(); it != sliceCache_.end(); ++it)
                        if (it->tex.valid() && it->lastFrame < frameNumber && (oldest == sliceCache_.end() || it->lastFrame < oldest->lastFrame)) oldest = it;
                    if (oldest == sliceCache_.end()) break;
                    bytes -= std::min<u64>(bytes, static_cast<u64>(oldest->w) * oldest->h * 8u);
                    backend_->destroy_texture(oldest->tex);
                    sliceCache_.erase(oldest);
                }
                sliceCache_.erase(std::remove_if(sliceCache_.begin(), sliceCache_.end(),
                    [](const SliceTexture& e) { return !e.tex.valid(); }), sliceCache_.end());
            }
            const Mat4 fold = (i < snap.plans.size() && snap.plans[i].hasFold) ? snap.plans[i].foldMatrix : Mat4::identity();
            // Eixo das faixas em uv da região: a projeção do retângulo inteiro
            // na direção pedida (px) cobre exatamente [0, 1].
            const Vec4 dir = layer.temporal.front().slice;
            const f32 rw = std::max(1e-3f, draw.region.w), rh = std::max(1e-3f, draw.region.h);
            const f32 denom = std::max(1e-6f, rw * std::fabs(dir.x) + rh * std::fabs(dir.y));
            const Vec2 axis{rw * dir.x / denom, rh * dir.y / denom};
            TextureDesc accDesc = compDesc;
            accDesc.transferSrc = false;
            const FGTexture acc = graph_.create_texture("timeslice", accDesc);
            struct SlicePush { Mat4 clipFromLayer; Vec4 region; Vec4 uvRect; Vec4 params; Vec4 slice; };
            static_assert(sizeof(SlicePush) <= binding::kPushConstantBytes, "push constants do time_slice.frag");
            struct Cap {
                const RenderLayer* layer; Mat4 fold; u32 k; PipelineHandle p; u64 sampler; Rect region;
                f32 w, h; const FGTexture* perSample; Vec2 axis; bool wide;
            } cap{&layer, fold, k, *pSlice, draw.sampler, draw.region, compW, compH, perSample, axis, wide};
            const u32 pass = graph_.add_raster_pass(wide ? "tempo-amplo" : "timeslice", PassStage::Composite, acc, LoadOp::Clear,
                                                    Vec4{0, 0, 0, 0}, [cap](PassContext& pc) {
                const Mat4 clip = clip_from_comp(cap.w, cap.h);
                pc.cmds.bind_pipeline(cap.p);
                for (u32 s = 0; s < cap.k; ++s) {
                    const auto& sample = cap.layer->temporal[s];
                    pc.cmds.bind_texture(0, pc.texture(cap.perSample[s]), SamplerHandle{cap.sampler});
                    if (cap.wide) {
                        LayerPush push;
                        push.clipFromLayer = clip * (sample.m * cap.fold);
                        push.params = Vec4{sample.weight, 0, 0, 0};
                        push.region = Vec4{cap.region.x, cap.region.y, cap.region.w, cap.region.h};
                        push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
                        pc.cmds.push_constants(&push, sizeof(push));
                        pc.cmds.draw(6);
                        continue;
                    }
                    SlicePush push;
                    push.clipFromLayer = clip * (sample.m * cap.fold);
                    push.region = Vec4{cap.region.x, cap.region.y, cap.region.w, cap.region.h};
                    push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
                    push.params = Vec4{sample.weight, 0, 0, 0};
                    push.slice = Vec4{cap.axis.x, cap.axis.y, sample.slice.z, sample.slice.w};
                    pc.cmds.push_constants(&push, sizeof(push));
                    pc.cmds.draw(6);
                }
            });
            for (u32 s = 0; s < k; ++s) {
                bool seen = false;
                for (u32 r = 0; r < s && !seen; ++r) seen = perSample[r] == perSample[s];
                if (!seen) graph_.read(pass, perSample[s]);
            }
            draw.texture = acc;
            draw.region = Rect{0, 0, compW, compH};
            draw.compFromLayer = Mat4::identity();
            draw.sampler = shaders_.sampler(CommonSampler::LinearClamp).id;
            return true;
        }
        if (!layer.blurMatrices.empty() || !layer.temporal.empty()) {
            // Acumula as amostras no tempo (pré-multiplicadas, com peso, soma
            // aditiva) num alvo do tamanho da composição: desfoque = média
            // (peso 1/K); eco = soma com queda; RGB = um canal por amostra.
            auto pAdd = shaders_.pipeline(PipelineKey::graphics(
                ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Add));
            if (pAdd.ok()) {
                const bool temporal = !layer.temporal.empty();
                const u32 k = static_cast<u32>(temporal ? layer.temporal.size() : layer.blurMatrices.size());
                const Mat4 fold = (i < snap.plans.size() && snap.plans[i].hasFold && !(layer.blurIncludesFold && layer.temporal.empty()))
                                ? snap.plans[i].foldMatrix : Mat4::identity();
                MotionBlurBounds bounds{Rect{0,0,compW,compH}, compDesc.width, compDesc.height};
                // The transformed source/effect rectangle encloses every pixel
                // emitted by a sample. Crop its union on the output pixel grid;
                // do not reduce sample count, exposure duration or weights.
                if (motionBlurCropEnabled_ && !temporal)
                    bounds = motion_blur_bounds(draw.region, layer.blurMatrices, fold,
                        snap.compWidth, snap.compHeight, compDesc.width, compDesc.height);
                TextureDesc accDesc = compDesc;
                accDesc.width = bounds.width; accDesc.height = bounds.height;
                accDesc.transferSrc = false;
                const FGTexture acc = graph_.create_texture("desfoque de movimento", accDesc);
                // ECO NUMA PRÉ-COMPOSIÇÃO: cada amostra é a filha NAQUELE
                // instante (com as máscaras e os efeitos da camada), não a
                // imagem de agora repetida — que somava N cópias iguais.
                FGTexture* perSample = nullptr;
                if (temporal && layer.source.kind == LayerSource::Kind::Nested && currentSnap_) {
                    perSample = arena_.alloc_array<FGTexture>(k);
                    for (u32 s = 0; perSample && s < k; ++s) {
                        perSample[s] = draw.texture;
                        const u32 n = layer.temporal[s].nested;
                        if (n == ~0u || n == layer.source.nestedIndex) continue;
                        const FrameSnapshot* other = n < currentSnap_->nested.size() ? currentSnap_->nested[n].get() : nullptr;
                        if (!other || !other->target.valid()) { mark_incomplete(__FILE__, __LINE__); continue; }
                        LayerImage sample = raw;
                        sample.texture = other->target;
                        sample.neighbors = TemporalNeighbors{};   // os vizinhos são do quadro de agora
                        apply_masks(layer, sample, frameNumber);
                        LayerImage sampleFin = sample;
                        if (hasEffects) (void)EffectGraph::build(snap.plans[i], ctx, sample, sampleFin);
                        perSample[s] = sampleFin.texture;
                    }
                }
                struct Cap {
                    const RenderLayer* layer; Mat4 fold; u32 k; PipelineHandle p; FGTexture src; u64 sampler; Rect region;
                    Rect bounds; const FGTexture* perSample;
                } cap{&layer, fold, k, *pAdd, draw.texture, draw.sampler, draw.region, bounds.region, perSample};
                const u32 pass = graph_.add_raster_pass("desfoque de movimento", PassStage::Composite, acc, LoadOp::Clear,
                                                        Vec4{0, 0, 0, 0}, [cap](PassContext& pc) {
                    const Mat4 clip = clip_from_comp(cap.bounds.w, cap.bounds.h)
                        * Mat4::translation(Vec3{-cap.bounds.x, -cap.bounds.y, 0});
                    pc.cmds.bind_pipeline(cap.p);
                    pc.cmds.bind_texture(0, pc.texture(cap.src), SamplerHandle{cap.sampler});
                    for (u32 s = 0; s < cap.k; ++s) {
                        if (cap.perSample) pc.cmds.bind_texture(0, pc.texture(cap.perSample[s]), SamplerHandle{cap.sampler});
                        LayerPush push;
                        // The snapshot stays alive through graph execution. Use
                        // its samples directly, without another per-layer copy.
                        if (!cap.layer->temporal.empty()) {
                            const auto& sample = cap.layer->temporal[s];
                            push.clipFromLayer = clip * (sample.m * cap.fold);
                            push.params = Vec4{sample.weight, sample.mask.x, sample.mask.y, sample.mask.z};
                        } else {
                            push.clipFromLayer = clip * (cap.layer->blurMatrices[s] * cap.fold);
                            push.params = Vec4{1.0f / static_cast<f32>(cap.k), 0, 0, 0};
                        }
                        push.region = Vec4{cap.region.x, cap.region.y, cap.region.w, cap.region.h};
                        push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
                        pc.cmds.push_constants(&push, sizeof(push));
                        pc.cmds.draw(6);
                    }
                });
                graph_.read(pass, draw.texture);
                if (perSample) for (u32 s = 0; s < k; ++s) if (perSample[s] != draw.texture) graph_.read(pass, perSample[s]);
                draw.texture = acc;
                draw.region = bounds.region;
                draw.compFromLayer = Mat4::identity();
                draw.sampler = shaders_.sampler(CommonSampler::LinearClamp).id;
            } else mark_incomplete(__FILE__, __LINE__);
        }
        return true;
    };
    // Camadas lidas como IMAGEM por efeitos (o mapa do Mapa de deslocamento):
    // antes de tudo, cada uma sozinha num alvo do tamanho da composição, no
    // mesmo instante. Só depois de todas desenhadas elas viram entrada — um
    // mapa que aponta de volta para quem o lê (A↔B) não entra em laço: lá
    // dentro a outra ainda não existe e o efeito devolve a imagem intacta.
    std::vector<EffectBuildContext::LayerInput> layerInputs;
    {
        std::vector<u64> wanted;
        for (u32 i = 0; i < snap.plans.size() && i < snap.layers.size(); ++i) {
            for (const EffectEval& ev : snap.plans[i].evals) {
                for (u32 slot = 0; ev.effect && slot < ev.effect->input_layer_count(); ++slot) {
                    const i32 k = ev.effect->input_layer_param_at(slot);
                    if (k >= 0 && static_cast<u32>(k) < ev.count && ev.values[k].ref) wanted.push_back(ev.values[k].ref);
                }
            }
        }
        for (u32 i = 0; i < snap.layers.size() && !wanted.empty(); ++i) {
            const RenderLayer& layer = snap.layers[i];
            if (layer.planeGroup >= 0 || layer.particle.inScene || layer.source.kind == LayerSource::Kind::Adjustment) continue;
            EffectBuildContext::LayerInput probe;
            probe.layer = layer.id.pack();
            bool want = false;
            for (u64 w : wanted) {
                const LayerId a = LayerId::unpack(w), b = layer.id;
                want |= w == probe.layer || ((b.index & 0x40000000u) && a.generation == b.generation
                                             && (a.index & 0xFFFFu) == (b.index & 0xFFFFu));
            }
            if (!want) continue;
            CompositeDraw d;
            if (!make_draw(i, d)) continue;
            const FGTexture t = draw_to_comp(d, compDesc, compW, compH, "camada-de-entrada");
            if (!t.valid()) continue;
            probe.image = LayerImage{t, Rect{0.0f, 0.0f, compW, compH}, compDesc.width, compDesc.height};
            layerInputs.push_back(probe);
        }
    }
    ctx.set_layer_inputs(layerInputs);
    // Track matte: as mattes primeiro, cada uma sozinha num alvo do tamanho
    // da composição (transform, opacidade, máscaras e efeitos dela). Elas não
    // entram na composição por conta própria.
    FGTexture* matteTex = snap.layers.empty() ? nullptr : arena_.alloc_array<FGTexture>(static_cast<u32>(snap.layers.size()));
    for (u32 i = 0; i < snap.layers.size(); ++i) {
        const RenderLayer& layer = snap.layers[i];
        matteTex[i] = FGTexture{};
        if (!layer.matteOnly || layer.planeGroup >= 0) continue;
        bool wanted = false;   // alguém neste quadro usa esta matte?
        for (const RenderLayer& o : snap.layers) wanted |= o.matteIndex == static_cast<i32>(i);
        if (!wanted) continue;
        CompositeDraw d;
        if (make_draw(i, d)) matteTex[i] = draw_to_comp(d, compDesc, compW, compH, "track-matte-fonte");
    }
    // Consume each finished source before declaring the next layer's effects.
    // Even four expanded Motion Tile/glow outputs can exhaust the remaining
    // headroom beside full-resolution scenes, precomps and decoded video.
    // The consumer is declared before later independent producers, so the
    // stable graph order releases its source and permits physical slot reuse.
    // Keeping every layer's
    // motion-blur accumulator alive until one final hardware blend required
    // 1.5 GiB for 96 full-HD RGBA16F layers. Normal blending is associative in
    // this order, so load the accumulated backdrop and reuse the finished
    // sources. Directed adjustments can rewrite earlier draws and therefore
    // keep the complete stack until their existing compositor runs.
    const bool streamNormal = snap.layers.size() >= 8
        && std::none_of(snap.layers.begin(), snap.layers.end(), [](const RenderLayer& layer) {
            return layer.source.kind == LayerSource::Kind::Adjustment || layer.blend != BlendMode::Normal;
        });
    u32 streamedDraws = 0;
    auto push_draw = [&](const CompositeDraw& d) {
        draws.push_back(d);
        if (streamNormal) {
            composite_draws(snap, comp, compDesc, draws, ctx, streamedDraws);
            streamedDraws = static_cast<u32>(draws.size());
        }
    };
    // MÁSCARA DE RECORTE: a base abre um grupo; as recortadas seguidas entram
    // nele como source-atop (cada uma com a própria mistura e opacidade). O
    // grupo monta num alvo próprio, transparente, e entra na cena com a
    // mistura e a opacidade da base. Sem nenhuma recortada desenhada no
    // quadro, a base vai sozinha, exatamente como sempre.
    u64 clipOpen = 0;                         // id da base do grupo aberto
    CompositeDraw clipBaseDraw;
    std::vector<CompositeDraw> clipDraws;
    auto close_clip = [&] {
        if (!clipOpen) return;
        clipOpen = 0;
        if (clipDraws.size() <= 1) {
            clipDraws.clear();
            push_draw(clipBaseDraw);
            return;
        }
        TextureDesc gd = compDesc;
        gd.transferSrc = false;
        gd.sampled = true;
        gd.renderTarget = true;
        const FGTexture group = graph_.create_texture("recorte-grupo", gd);
        composite_draws(snap, group, compDesc, clipDraws, ctx, 0, true);
        clipDraws.clear();
        CompositeDraw g;
        g.texture = group;
        g.region = Rect{0.0f, 0.0f, compW, compH};
        g.compFromLayer = Mat4::identity();
        g.opacity = clipBaseDraw.opacity;
        g.blend = clipBaseDraw.blend;
        g.sampler = shaders_.sampler(CommonSampler::LinearClamp).id;
        g.layer = clipBaseDraw.layer;
        push_draw(g);
    };
    for (u32 i = 0; i < snap.layers.size(); ++i) {
        const RenderLayer& layer = snap.layers[i];
        if (layer.planeGroup >= 0 || layer.matteOnly || layer.particle.inScene) continue;   // na cena 3D / só recorta
        if (!layer.clipBase) close_clip();   // fora da cadeia: o grupo aberto termina aqui
        CompositeDraw draw;
        if (layer.source.kind == LayerSource::Kind::Adjustment && (layer.adjustScope == 1 || layer.adjustScope == 2)) {
            // Ajuste SÓ em camadas escolhidas: a logo abaixo (escopo 1; um
            // grupo conta como uma) ou as da lista (escopo 2, a do app
            // antigo). Cada uma vai sozinha para um alvo do tamanho da
            // composição, os efeitos rodam nela e o resultado toma o lugar
            // dela na pilha. A opacidade do ajuste mistura original e ajustado.
            auto adjust_draw = [&](usize at, u32 target) {
                const CompositeDraw below = draws[at];
                const FGTexture lc = draw_to_comp(below, compDesc, compW, compH, "ajuste-camada-abaixo");
                if (!lc.valid()) return;
                LayerImage in;
                in.texture = lc;
                in.region = Rect{0.0f, 0.0f, compW, compH};
                in.width = compDesc.width;
                in.height = compDesc.height;
                // Efeitos de tempo: a camada (só ela) noutros instantes.
                adjust_time_input(snap, layer, target, in, compDesc, frameNumber);
                LayerImage fin = in;
                const EffectPlan& plan = snap.plans[i];
                (void)EffectGraph::build(plan, ctx, in, fin);
                if (!fin.valid()) return;
                const bool masked = layer.maskCount > 0;
                if (masked) apply_masks(layer, fin, frameNumber);
                CompositeDraw orig;
                orig.texture = lc;
                orig.region = Rect{0.0f, 0.0f, compW, compH};
                orig.blend = below.blend;
                orig.sampler = shaders_.sampler(CommonSampler::LinearClamp).id;
                orig.layer = below.layer;
                CompositeDraw fx = orig;
                fx.texture = fin.texture;
                fx.region = fin.region;
                if (plan.hasFold) fx.compFromLayer = plan.foldMatrix;
                // A opacidade da camada de ajuste mistura original e ajustado;
                // a dobrada (Piscar, Cintilar, opacidade do Transform) é a do
                // conteúdo ajustado, como numa pré-composição.
                const f32 op = std::clamp(layer.opacity, 0.0f, 1.0f);
                const f32 content = std::clamp(plan.hasFold ? plan.foldOpacity : 1.0f, 0.0f, 1.0f);
                if (masked) {
                    // Com máscara: o original inteiro e, por cima, o ajustado
                    // recortado (transparente fora da máscara) na opacidade.
                    fx.opacity = op * content;
                    draws[at] = orig;
                    draws.insert(draws.begin() + static_cast<std::ptrdiff_t>(at) + 1, fx);
                } else if (op >= 0.999f) {
                    fx.opacity = content;
                    draws[at] = fx;
                } else {
                    orig.opacity = 1.0f - op;
                    fx.opacity = op * content;
                    draws[at] = orig;
                    draws.insert(draws.begin() + static_cast<std::ptrdiff_t>(at) + 1, fx);
                }
            };
            if (layer.adjustScope == 1) {
                if (!draws.empty() && draws.back().adjustPlan == kInvalidIndex) adjust_draw(draws.size() - 1, 0);
                continue;
            }
            for (usize at = draws.size(); at-- > 0;) {
                const CompositeDraw& d = draws[at];
                if (d.adjustPlan != kInvalidIndex || !d.layer) continue;
                const auto hit = std::find(layer.adjustTargets.begin(), layer.adjustTargets.end(), d.layer);
                if (hit == layer.adjustTargets.end()) continue;
                adjust_draw(at, static_cast<u32>(hit - layer.adjustTargets.begin()));
            }
            continue;
        }
        if (layer.source.kind == LayerSource::Kind::Adjustment) {
            // Camada de ajuste: montada na composição, sobre o fundo acumulado.
            draw.adjustPlan = i;
            draw.opacity = layer.opacity;
            draw.blend = layer.blend;
            draws.push_back(draw);
            continue;
        }
        if (layer.matteMode != MatteMode::None) {
            const bool inverted = layer.matteMode == MatteMode::AlphaInverted || layer.matteMode == MatteMode::LumaInverted;
            const FGTexture m = layer.matteIndex >= 0 ? matteTex[layer.matteIndex] : FGTexture{};
            // Sem matte no instante: nada aparece através dela (ou tudo, no invertido).
            if (!m.valid() && !inverted) continue;
            if (!make_draw(i, draw)) continue;
            if (m.valid()) {
                const FGTexture lc = draw_to_comp(draw, compDesc, compW, compH, "track-matte-camada");
                TextureDesc td = compDesc;
                td.transferSrc = false;
                const FGTexture out = graph_.create_texture("track-matte", td);
                const Vec4 mode{static_cast<f32>(static_cast<u8>(layer.matteMode)), 0, 0, 0};
                if (!lc.valid() || ctx.fullscreen_pass("track-matte", PassStage::Composite, out, ShaderId::mask_track_matte_frag,
                                                       {PassTexture{lc}, PassTexture{m}}, &mode, sizeof(mode)) == kInvalidIndex) {
                    mark_incomplete(__FILE__, __LINE__);
                    continue;
                }
                draw.texture = out;
                draw.region = Rect{0.0f, 0.0f, compW, compH};
                draw.compFromLayer = Mat4::identity();
                draw.opacity = 1.0f;   // já aplicada no alvo da camada
                draw.sampler = shaders_.sampler(CommonSampler::LinearClamp).id;
            }
        } else if (!make_draw(i, draw)) {
            continue;
        }
        draw.layer = layer.id.pack();
        if (layer.clipBase) {
            // A base não desenhou neste quadro (escondida, fora da tela, matte):
            // a recortada some com ela.
            if (layer.clipBase != clipOpen) continue;
            draw.clipAtop = true;
            clipDraws.push_back(draw);
            continue;
        }
        if (layer.clipParent) {
            clipOpen = draw.layer;
            clipBaseDraw = draw;
            CompositeDraw inner = draw;   // dentro do grupo: a base inteira, sem a mistura dela
            inner.opacity = 1.0f;
            inner.blend = BlendMode::Normal;
            clipDraws.assign(1, inner);
            continue;
        }
        push_draw(draw);
    }
    close_clip();

    if (!streamedDraws || streamedDraws < draws.size())
        composite_draws(snap, comp, compDesc, draws, ctx, streamedDraws);
}

// =============================================================================
// Composição da pilha
//
// Normal é blend de hardware: todos os desenhos Normal seguidos vão num passe
// só, no mesmo alvo (nenhuma leitura do destino). Add também passa pelo shader:
// a soma de hardware somava o ALFA (1 + 0,6 = 1,6 sobre fundo opaco) e quem
// despré-multiplica depois (captura, export) escurecia a cor. Um modo que
// precisa do fundo (Add, Multiply, Overlay, Hue...) ou uma camada de ajuste corta
// o lote: o passe seguinte escreve um alvo NOVO com a cópia do fundo e, por
// cima, o quad da camada com blend.frag lendo o fundo como textura. O último
// desses passes escreve direto no alvo final (nenhuma cópia extra no fim) e
// o que sobra da pilha continua nele com LoadOp::Load. Sem nenhum modo desses
// no quadro, o caminho é exatamente o de antes: um passe, zero texturas a mais.
// =============================================================================
void Renderer::composite_draws(FrameSnapshot& snap, FGTexture comp, const TextureDesc& compDesc,
                               const std::vector<CompositeDraw>& draws, EffectBuildContext& ctx,
                               u32 firstDraw, bool isolated) noexcept {
    auto pNormal = shaders_.pipeline(PipelineKey::graphics(
        ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Normal));
    auto pAdd = shaders_.pipeline(PipelineKey::graphics(
        ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Add));
    const f32 compW = static_cast<f32>(snap.compWidth), compH = static_cast<f32>(snap.compHeight);
    const u32 count = static_cast<u32>(draws.size());
    auto hardware = [](const CompositeDraw& d) noexcept {
        // Recortada: source-atop lê o alfa do destino (passe com blend.frag).
        return d.adjustPlan == kInvalidIndex && d.blend == BlendMode::Normal && !d.clipAtop;
    };
    u32 lastRead = kInvalidIndex;   // último desenho que lê o fundo
    for (u32 i = firstDraw; i < count; ++i) if (!hardware(draws[i])) lastRead = i;
    PipelineHandle copyP{}, blendP{};
    if (lastRead != kInvalidIndex) {
        auto c = shaders_.pipeline(PipelineKey::graphics(ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat));
        auto b = shaders_.pipeline(PipelineKey::graphics(ShaderId::composite_layer_vert, ShaderId::composite_blend_frag, kWorkFormat));
        if (c.ok() && b.ok()) { copyP = *c; blendP = *b; }
        else mark_incomplete(__FILE__, __LINE__);   // pipeline compilando: este quadro cai no Normal, o próximo tenta de novo
    }
    const bool readsBackdrop = lastRead != kInvalidIndex && copyP.valid() && blendP.valid();
    // Máscara/Excluir (AM) recortam as CAMADAS abaixo, não a cor de fundo da
    // composição: a pilha monta sobre transparente e o fundo entra por baixo
    // num último passe (fundo + resultado "sobre" ele).
    const bool bgUnder = !isolated && readsBackdrop && pNormal.ok() && firstDraw == 0 && snap.background.w > 0.0f
        && std::any_of(draws.begin() + firstDraw, draws.end(), [](const CompositeDraw& d) {
               return d.adjustPlan == kInvalidIndex && (d.blend == BlendMode::Mask || d.blend == BlendMode::Exclude);
           });
    // Grupo de recorte: isolado sobre transparente (o fundo entra com a cena).
    const Vec4 stackClear = bgUnder || isolated ? Vec4{0.0f, 0.0f, 0.0f, 0.0f} : snap.background;
    TextureDesc pingDesc = compDesc;
    pingDesc.transferSrc = false;
    pingDesc.sampled = true;
    pingDesc.renderTarget = true;
    // O alvo final (import do export/captura) pode não ser amostrável: o
    // fundo lido é sempre uma textura do grafo.
    FGTexture cur = readsBackdrop ? graph_.create_texture("composicao-fundo", pingDesc) : comp;
    bool fresh = firstDraw == 0; // later normal batches load the existing backdrop
    u32 batchBegin = firstDraw;

    struct HwCap {
        CompositeDraw* draws; u32 count; PipelineHandle normal; PipelineHandle add; f32 compW; f32 compH;
    };
    auto flush = [&](u32 end) {
        if (end == batchBegin && !fresh) return;
        u32 n = 0;
        for (u32 i = batchBegin; i < end; ++i) n += draws[i].adjustPlan == kInvalidIndex ? 1u : 0u;
        CompositeDraw* arr = n ? arena_.alloc_array<CompositeDraw>(n) : nullptr;
        u32 k = 0;
        for (u32 i = batchBegin; i < end; ++i) if (draws[i].adjustPlan == kInvalidIndex) arr[k++] = draws[i];
        const HwCap cap{arr, n, pNormal.ok() ? *pNormal : PipelineHandle{}, pAdd.ok() ? *pAdd : PipelineHandle{}, compW, compH};
        const u32 pass = graph_.add_raster_pass("composicao", PassStage::Composite, cur, fresh ? LoadOp::Clear : LoadOp::Load,
                                                stackClear, [cap](PassContext& pc) {
            const Mat4 clip = clip_from_comp(cap.compW, cap.compH);
            PipelineHandle bound{};
            for (u32 i = 0; i < cap.count; ++i) {
                const CompositeDraw& d = cap.draws[i];
                // Os modos que leem o fundo só chegam aqui se o pipeline deles
                // ainda não existe: Add cai na soma de hardware, o resto no Normal.
                // Máscara/Excluir sem o pipeline: não pinta (a camada nunca aparece).
                if (d.blend == BlendMode::Mask || d.blend == BlendMode::Exclude) continue;
                // Recortada sem o pipeline: não pinta (por cima sairia sem recorte).
                if (d.clipAtop) continue;
                const PipelineHandle p = d.blend == BlendMode::Add && cap.add.valid() ? cap.add : cap.normal;
                if (!p.valid()) continue;
                if (!(p == bound)) { pc.cmds.bind_pipeline(p); bound = p; }
                LayerPush push;
                push.clipFromLayer = clip * d.compFromLayer;
                push.region = Vec4{d.region.x, d.region.y, d.region.w, d.region.h};
                push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
                push.params = Vec4{d.opacity, 0.0f, 0.0f, 0.0f};
                pc.cmds.bind_texture(0, pc.texture(d.texture), SamplerHandle{d.sampler});
                pc.cmds.push_constants(&push, sizeof(push));
                pc.cmds.draw(6);
            }
        });
        for (u32 i = 0; i < n; ++i) graph_.read(pass, arr[i].texture);
        fresh = false;
    };

    struct ReadCap {
        PipelineHandle copy, blend;
        FGTexture backdrop{}, src{};
        u64 copySampler = 0, srcSampler = 0;
        Mat4 compFromLayer = Mat4::identity();
        Vec4 region{};
        f32 opacity = 1.0f, mode = 0.0f, compW = 0.0f, compH = 0.0f;
        f32 masked = 0.0f;   ///< ajuste com máscara: fora dela o fundo fica
        f32 atop = 0.0f;     ///< recortada: source-atop (alfa do grupo = o da base)
        bool drawSrc = false;
        bool copyBackdrop = true;   ///< false = BlendMode::Mask: fora da camada fica transparente
    };
    for (u32 i = firstDraw; readsBackdrop && i < count; ++i) {
        const CompositeDraw& d = draws[i];
        if (hardware(d)) continue;
        flush(i);
        batchBegin = i + 1;
        ReadCap* rc = arena_.alloc_array<ReadCap>(1);
        new (rc) ReadCap{};
        rc->copy = copyP;
        rc->blend = blendP;
        rc->backdrop = cur;
        rc->copySampler = shaders_.sampler(CommonSampler::NearestClamp).id;
        rc->compW = compW;
        rc->compH = compH;
        rc->opacity = d.opacity;
        if (d.adjustPlan != kInvalidIndex) {
            // Os efeitos da camada de ajuste sobre o fundo acumulado (px da
            // composição, na resolução do alvo). Resultado misturado ao fundo
            // pela opacidade.
            LayerImage in;
            in.texture = cur;
            in.region = Rect{0.0f, 0.0f, compW, compH};
            in.width = compDesc.width;
            in.height = compDesc.height;
            const RenderLayer* adjustLayer = d.adjustPlan < snap.layers.size() ? &snap.layers[d.adjustPlan] : nullptr;
            // Efeitos de tempo / que leem o alfa: o fundo noutro instante ou
            // sem a cor de fundo (ver RenderLayer::AdjustTime).
            if (adjustLayer) adjust_time_input(snap, *adjustLayer, 0, in, compDesc, frameNumber_);
            const bool bare = adjustLayer && !adjustLayer->adjustTime.empty() && adjustLayer->adjustTime[0].bare != ~0u;
            LayerImage fin = in;
            const EffectPlan& plan = snap.plans[d.adjustPlan];
            (void)EffectGraph::build(plan, ctx, in, fin);
            if (fin.valid() && (plan.hasFold || bare)) {
                // A geometria dobrada (Transform, Agitar, Oscilar, Tremor... no
                // fim da pilha) leva o fundo INTEIRO, e o que ela descobre
                // mostra a cor de fundo — antes a borda descoberta mantinha o
                // fundo parado por baixo (imagem dupla) e só Transform na
                // pilha nem chegava aqui. Sem fundo (efeito de alfa), a cor de
                // fundo volta por baixo do resultado.
                // A opacidade dobrada (Piscar, Cintilar, opacidade do
                // Transform) é a do CONTEÚDO, como numa pré-composição: o
                // fundo aparece por baixo.
                const FGTexture full = adjust_full_frame(fin, plan.hasFold ? plan.foldMatrix : Mat4::identity(),
                                                         plan.hasFold ? plan.foldOpacity : 1.0f,
                                                         snap.background, compDesc, compW, compH);
                if (full.valid()) {
                    fin.texture = full;
                    fin.region = Rect{0.0f, 0.0f, compW, compH};
                    fin.width = compDesc.width;
                    fin.height = compDesc.height;
                }
            }
            if (adjustLayer && adjustLayer->maskCount > 0 && fin.valid() && !(fin.texture == cur)) {
                apply_masks(*adjustLayer, fin, frameNumber_);
                rc->masked = 1.0f;
            }
            rc->drawSrc = fin.valid() && !(fin.texture == cur) ;
            rc->src = fin.texture;
            rc->region = Vec4{fin.region.x, fin.region.y, fin.region.w, fin.region.h};
            rc->compFromLayer = Mat4::identity();
            rc->opacity = d.opacity;
            rc->srcSampler = shaders_.sampler(CommonSampler::LinearClamp).id;
            // 100 + modo: a mistura da camada de ajuste com o modo dela.
            rc->mode = 100.0f + static_cast<f32>(static_cast<u16>(d.blend));
        } else {
            rc->drawSrc = d.texture.valid();
            rc->src = d.texture;
            rc->region = Vec4{d.region.x, d.region.y, d.region.w, d.region.h};
            rc->compFromLayer = d.compFromLayer;
            rc->srcSampler = d.sampler;
            rc->mode = static_cast<f32>(static_cast<u16>(d.blend));
            // Máscara (AM): só sobra o fundo sob o alfa da camada — o que
            // está fora do quad dela some, então o fundo não é copiado.
            rc->copyBackdrop = d.blend != BlendMode::Mask;
            rc->atop = d.clipAtop ? 1.0f : 0.0f;
        }
        const FGTexture next = i == lastRead && !bgUnder ? comp : graph_.create_texture("composicao-mistura", pingDesc);
        const u32 pass = graph_.add_raster_pass("mistura", PassStage::Composite, next, LoadOp::Clear, Vec4{0, 0, 0, 0},
                                                [rc](PassContext& pc) {
            const Mat4 clip = clip_from_comp(rc->compW, rc->compH);
            // 1) o fundo, texel a texel (mesmo tamanho, amostra no centro).
            LayerPush push;
            if (rc->copyBackdrop) {
            pc.cmds.bind_pipeline(rc->copy);
            push.clipFromLayer = clip;
            push.region = Vec4{0.0f, 0.0f, rc->compW, rc->compH};
            push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
            push.params = Vec4{1.0f, 0.0f, 0.0f, 0.0f};
            pc.cmds.bind_texture(0, pc.texture(rc->backdrop), SamplerHandle{rc->copySampler});
            pc.cmds.push_constants(&push, sizeof(push));
            pc.cmds.draw(6);
            }
            if (!rc->drawSrc) return;
            // 2) a camada, com a cor final calculada contra o fundo.
            pc.cmds.bind_pipeline(rc->blend);
            push.clipFromLayer = clip * rc->compFromLayer;
            push.region = rc->region;
            push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};   // também sem a cópia do fundo (Máscara)
            push.params = Vec4{rc->opacity, rc->mode, rc->masked, rc->atop};
            pc.cmds.bind_texture(0, pc.texture(rc->src), SamplerHandle{rc->srcSampler});
            pc.cmds.bind_texture(1, pc.texture(rc->backdrop), SamplerHandle{rc->copySampler});
            pc.cmds.push_constants(&push, sizeof(push));
            pc.cmds.draw(6);
        });
        graph_.read(pass, cur);
        if (rc->drawSrc && !(rc->src == cur)) graph_.read(pass, rc->src);
        cur = next;
        fresh = false;
    }
    flush(count);
    if (bgUnder && pNormal.ok() && !(cur == comp)) {
        // A cor de fundo por baixo da pilha já recortada pelas máscaras.
        struct BgCap { PipelineHandle p; FGTexture src; u64 sampler; f32 compW, compH; };
        const BgCap bc{*pNormal, cur, shaders_.sampler(CommonSampler::NearestClamp).id, compW, compH};
        const u32 pass = graph_.add_raster_pass("composicao-fundo-final", PassStage::Composite, comp, LoadOp::Clear,
                                                snap.background, [bc](PassContext& pc) {
            pc.cmds.bind_pipeline(bc.p);
            LayerPush push;
            push.clipFromLayer = clip_from_comp(bc.compW, bc.compH);
            push.region = Vec4{0.0f, 0.0f, bc.compW, bc.compH};
            push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
            push.params = Vec4{1.0f, 0.0f, 0.0f, 0.0f};
            pc.cmds.bind_texture(0, pc.texture(bc.src), SamplerHandle{bc.sampler});
            pc.cmds.push_constants(&push, sizeof(push));
            pc.cmds.draw(6);
        });
        graph_.read(pass, cur);
    }
}

void Renderer::prepare_raw(LayerId id, const Layer& layer, const Asset& asset, i64 mediaUs,
                           MediaManager& media, u64 frameNumber, DecodeMode mode,
                           i32 direction, f32 speed, u64 epoch, FrameSnapshot& out) {
    if (previewGraph_.active) return;
    out = FrameSnapshot{};
    out.compWidth = std::max(1u, asset.video.width);
    out.compHeight = std::max(1u, asset.video.height);
    out.videoLayers = 1;
    RenderLayer video;
    video.id = id;
    video.source.kind = LayerSource::Kind::Video;
    // finalQuality=true here means original media, never a proxy.
    if (auto* source = media.source_for(id, layer.source, asset, frameNumber, true)) {
        source->set_epoch(epoch);
        const auto info = source->info();
        out.compWidth = std::max(1u, info.display_width());
        out.compHeight = std::max(1u, info.display_height());
        source->cache().set_required_times(&mediaUs, 1, source->frame_duration_us() / 2);
        DecodeRequest request;
        request.targetUs = mediaUs;
        request.mode = mode;
        request.direction = direction;
        request.speed = speed;
        source->request(request);
        video.source.frame = source->frame_for(mediaUs, &video.source.frameExact);
        out.playbackStream = info;
        out.playbackDecode = source->stats();
        out.playbackTargetUs = mediaUs;
        if (video.source.frame) {
            out.playbackPtsUs = video.source.frame->ptsUs;
            out.playbackDurationUs = video.source.frame->durationUs;
            out.playbackPixelFormat = static_cast<u32>(video.source.frame->format);
        }
    }
    video.source.width = out.compWidth;
    video.source.height = out.compHeight;
    out.missingVideoFrames = !video.source.frame;
    out.staleVideoFrames = video.source.frame && !video.source.frameExact;
    out.layers.push_back(std::move(video));
}

Status Renderer::prepare_capture_resources(FrameSnapshot& snapshot, const RenderSettings& settings) noexcept {
    if (!backend_ || !settings.finalQuality) return Errc::InvalidState;
    if (previewGraph_.active) return Errc::Busy;
    const auto cancelled = [&] {
        return settings.cancelFlag && settings.cancelFlag->load(std::memory_order_acquire);
    };
    if (cancelled()) return Errc::Cancelled;
    const bool traceEnabled = settings.offscreenFenceDeadline != nullptr;
    struct EnvironmentTraceScope {
        scene3d::SceneRenderer& scene;
        bool previous;
        EnvironmentTraceScope(scene3d::SceneRenderer& value, bool enabled) noexcept
            : scene(value), previous(scene.set_environment_trace(enabled)) {}
        ~EnvironmentTraceScope() { (void)scene.set_environment_trace(previous); }
    } trace(scene3d_, traceEnabled);
    quality_ = effective_quality(settings);
    heavyQ_ = resolve_heavy(settings);
    const u64 pipelinesStarted = traceEnabled ? monotonic_ns() : 0;
    warm_scene_pipelines(snapshot);
    flush_warmup();
    if (traceEnabled && monotonic_ns() - pipelinesStarted > 5000000ull)
        AUREA_LOG_INFO("capture-resource pipeline-warmup_ms=%.3f", static_cast<double>(monotonic_ns() - pipelinesStarted) * 1e-6);
    if (cancelled()) return Errc::Cancelled;
    // Mark all required environments as recent in the same next main frame.
    // Serial jobs reuse ordinary GPU caches instead of retaining a second set
    // of full-resolution CPU maps for every nested composition/object.
    const u64 frame = backend_->last_submitted_frame() + 1;
    const auto instances = [&](const scene3d::SceneFrame& group) -> Status {
        for (const auto& instance : group.instances) if (instance.ownEnvironment) {
            if (cancelled()) return Errc::Cancelled;
            const Status status = scene3d_.prepare_environment(instance.environment, frame, true);
            if (!status.ok()) return status;
        }
        return OkStatus;
    };
    const auto visit = [&](auto&& self, const FrameSnapshot& value) -> Status {
        for (const auto& group : value.scenes) {
            if (cancelled()) return Errc::Cancelled;
            configure_scene_quality(group);
            const Status environment = scene3d_.prepare_environment(group.environment, frame);
            if (!environment.ok()) return environment;
            const Status main = instances(group);
            if (!main.ok()) return main;
            for (const auto& sample : group.blurFrames) {
                const Status subframe = instances(sample);
                if (!subframe.ok()) return subframe;
            }
        }
        for (const auto& child : value.nested) if (child) {
            const Status nested = self(self, *child);
            if (!nested.ok()) return nested;
        }
        return OkStatus;
    };
    const Status ready = visit(visit, snapshot);
    return ready;
}

Status Renderer::render(FrameSnapshot& snap, const RenderSettings& settings,
                        const OffscreenTarget* offscreen, FrameStats& stats,
                        RenderTimings& timings) noexcept {
    if (!backend_) return Status{Errc::InvalidState, "renderer sem backend"};
    CaptureRenderPhases capturePhases(offscreen && settings.offscreenFenceDeadline);
    struct EnvironmentTraceScope {
        scene3d::SceneRenderer& scene;
        bool previous;
        EnvironmentTraceScope(scene3d::SceneRenderer& value, bool enabled) noexcept
            : scene(value), previous(scene.set_environment_trace(enabled)) {}
        ~EnvironmentTraceScope() { (void)scene.set_environment_trace(previous); }
    } environmentTrace(scene3d_, capturePhases.enabled);
    if (previewGraph_.active) return Status{Errc::Busy, "preview graph continuation active"};
    if (settings.cancelFlag && settings.cancelFlag->load(std::memory_order_acquire)) {
        snap.release_video_frames(); return Errc::Cancelled;
    }
    if (const Status ready = reclaim_failed_transients(); !ready.ok()) {
        snap.release_video_frames(); return ready;
    }
    capturePhases.mark("reclaim");
    // Pipelines que a varredura do projeto pediu: antes de pegar a imagem da
    // swapchain (compilar segurando a imagem atrasaria a apresentação).
    quality_ = effective_quality(settings);
    heavyQ_ = resolve_heavy(settings);
    if (!settings.rawPlayback && !settings.cachedComposition.valid()) {
        warm_scene_pipelines(snap);
        flush_warmup();
    }
    capturePhases.mark("pipeline-warmup");
    const u64 t0 = monotonic_ns();
    const u64 effectsBypassedBefore = EffectGraph::bypassed_total();
    scene3d_.reset_incomplete();
    videoSourceFailed_ = false;
    sliceApprox_ = false;
    videoUploadMs_ = 0;
    heavyScale_ = settings.finalQuality ? 1.0f : settings.heavyScale;
    heavyStats_.lastParticleSlots = heavyStats_.lastParticleLayers = 0;
    heavyStats_.lastParticleExtraLayers = 0;
    heavyStats_.lastSceneDrawCalls = heavyStats_.lastSceneShadowDrawCalls = heavyStats_.lastSceneInstancedDraws = 0;
    heavyStats_.lastSceneVisible = heavyStats_.lastSceneCulled = heavyStats_.lastSceneTriangles = 0;
    heavyStats_.lastShadowMapSize = 0;

    struct CompletedSceneExposures {
        Renderer& renderer;
        std::unique_ptr<TextureHandle[]> storage;
        std::span<TextureHandle> textures{};
        ~CompletedSceneExposures() {
            renderer.boundedSceneTextures_ = {};
            for (const auto texture : textures) if (texture.valid()) renderer.pool_.release(texture);
        }
    } completed{*this};
    FrameStats exposureStats;
    timings.sceneExposureRecordMs = timings.sceneExposureWaitMs = 0;
    timings.sceneExposureBatches = 0;
    timings.gpuProfileFragmented = false;
    struct CompletedSceneTargets {
        Renderer& renderer;
        std::unique_ptr<StagedSceneTarget[]> storage;
        std::span<StagedSceneTarget> targets{};
        ~CompletedSceneTargets() {
            renderer.stagedSceneTargets_ = {};
            for (const auto& target : targets) if (target.texture.valid()) {
                if (target.pendingFrame) renderer.pool_.retire(target.texture, target.pendingFrame);
                else renderer.pool_.release(target.texture);
            }
        }
    } staged{*this};
    if (settings.stageUnblurredScenes && settings.finalQuality && offscreen && offscreen->texture.valid()) {
        const u32 count = unblurred_scene_target_count(snap, 0);
        if (count) {
            staged.storage.reset(new (std::nothrow) StagedSceneTarget[count]{});
            if (!staged.storage) { mark_incomplete(__FILE__, __LINE__); snap.release_video_frames(); return Errc::OutOfMemory; }
            staged.targets = {staged.storage.get(), count};
            const Status status = render_staged_scene_targets(snap, settings, offscreen->width, offscreen->height,
                staged.targets, exposureStats, timings);
            if (!status.ok()) {
                if (status.code() == Errc::OutOfDeviceMemory) failedGraphFrame_ = backend_->last_submitted_frame();
                mark_incomplete(__FILE__, __LINE__); snap.release_video_frames(); return status;
            }
            // Cold scene resources retain the existing exact-capture retry
            // contract. Do not present or read an unfinished stage as pixels.
            if (scene3d_.incomplete() || std::any_of(staged.targets.begin(), staged.targets.end(),
                [](const StagedSceneTarget& target) { return !target.texture.valid(); })) {
                mark_incomplete(__FILE__, __LINE__); snap.release_video_frames(); return OkStatus;
            }
            stagedSceneTargets_ = staged.targets;
        }
    }
    if (!snap.sceneExposureBatches.empty()) {
        completed.storage.reset(new (std::nothrow) TextureHandle[snap.scenes.size()]{});
        if (!completed.storage) {
            mark_incomplete(__FILE__, __LINE__); snap.release_video_frames();
            return Status{Errc::OutOfMemory, "resultados de exposicao 3D"};
        }
        completed.textures = {completed.storage.get(), snap.scenes.size()};
        const u32 num = std::max(1u, settings.previewNumerator), den = std::max(1u, settings.previewDenominator);
        u32 width = offscreen && offscreen->texture.valid() ? offscreen->width : std::max(1u, snap.compWidth * num / den);
        u32 height = offscreen && offscreen->texture.valid() ? offscreen->height : std::max(1u, snap.compHeight * num / den);
        const u32 maxTexture = backend_->capabilities().maxTexture2D;
        if ((!offscreen || !offscreen->texture.valid()) && (width > maxTexture || height > maxTexture)) {
            const f32 factor = static_cast<f32>(maxTexture) / std::max(width, height);
            width = std::max(1u, static_cast<u32>(width * factor)); height = std::max(1u, static_cast<u32>(height * factor));
        }
        const Status prepared = render_bounded_scene_exposures(snap, settings, width, height,
            completed.textures, exposureStats, timings);
        if (!prepared.ok()) {
            if (prepared.code() == Errc::OutOfDeviceMemory) failedGraphFrame_ = backend_->last_submitted_frame();
            mark_incomplete(__FILE__, __LINE__); snap.release_video_frames(); return prepared;
        }
        boundedSceneTextures_ = completed.textures;
    }
    capturePhases.mark("scene-stages");

    // SceneStatsSink reports cumulative builds within the main GPU frame.
    // Merge completed earlier submissions once, after that sink has run.
    const HeavyStats stagedStats = heavyStats_;
    heavyStats_.lastSceneDrawCalls = heavyStats_.lastSceneShadowDrawCalls = heavyStats_.lastSceneInstancedDraws = 0;
    heavyStats_.lastSceneVisible = heavyStats_.lastSceneCulled = heavyStats_.lastSceneTriangles = 0;
    heavyStats_.lastShadowMapSize = 0;

    if (settings.cancelFlag && settings.cancelFlag->load(std::memory_order_acquire)) {
        snap.release_video_frames(); return Errc::Cancelled;
    }

    FrameBegin fb;
    // Alvo offscreen (export, captura): frame SEM swapchain. Com begin_frame o
    // backend adquiria uma imagem da tela e a apresentava vazia a cada quadro
    // exportado — e disputava a superfície com o ciclo de vida do app.
    const bool offscreenFrame = (offscreen && offscreen->texture.valid()) || settings.previewCacheOnly;
    const u64 acquireStarted = monotonic_ns();
    if (const Status s = offscreenFrame ? backend_->begin_offscreen_frame(fb) : backend_->begin_frame(fb); !s.ok()) {
        // Sem imagem de swapchain neste vsync (superfície em recriação): os
        // frames de vídeo deste snapshot só são soltos — não houve GPU.
        snap.release_video_frames();
        return s;
    }
    const u64 tBegin = monotonic_ns();
    timings.acquireWaitMs = static_cast<f32>(static_cast<f64>(tBegin - acquireStarted) * 1e-6);
    capturePhases.mark("begin-frame");

    // RAW never touches the text subsystem.
    if (!settings.rawPlayback) {
        u64 gen = 0;
        bool dirty = false;
        const u8* px = text::glyph_atlas(gen, dirty);
        if (!glyphAtlas_.valid()) {
            TextureDesc d;
            d.width = d.height = text::kGlyphAtlasSize;
            d.format = SurfaceFormat::R8;
            d.sampled = true;
            d.transferDst = true;
            d.debugName = "atlas-de-glifos";
            auto t = backend_->create_texture(d);
            if (t.ok()) glyphAtlas_ = *t;
            dirty = true;
        }
        if (glyphAtlas_.valid() && (dirty || gen != glyphAtlasGen_)) {
            PendingUpload up;
            up.texture = glyphAtlas_;
            up.bytesPerRow = text::kGlyphAtlasSize;
            up.data.assign(px, px + static_cast<usize>(text::kGlyphAtlasSize) * text::kGlyphAtlasSize);
            uploads_.push_back(std::move(up));
            glyphAtlasGen_ = gen;
            text::glyph_atlas_clean();
            ++heavyStats_.glyphAtlasUploads;
            heavyStats_.glyphAtlasUploadBytes += static_cast<u64>(text::kGlyphAtlasSize) * text::kGlyphAtlasSize;
        }
        u32 rasterized = 0, resets = 0;
        text::glyph_atlas_stats(rasterized, resets);
        heavyStats_.glyphsRasterized += rasterized - glyphRasterSeen_;
        heavyStats_.glyphAtlasResets += resets - glyphResetSeen_;
        glyphRasterSeen_ = rasterized;
        glyphResetSeen_ = resets;
    }
    flush_uploads();
    capturePhases.mark("uploads");
    pool_.begin_frame(*backend_, fb.frameNumber);
    graph_.reset();
    arena_.reset();
    draws_.clear();
    lastZeroCopy_ = false;
    framesInFlight_.clear();

    // --- Alvo da composição: resolução de PREVIEW, coordenadas de COMPOSIÇÃO.
    u32 cw = 0, ch = 0;
    if (settings.cachedComposition.valid()) {
        const auto desc = backend_->texture_desc(settings.cachedComposition);
        cw = desc.width; ch = desc.height;
    } else if (offscreen && offscreen->texture.valid()) {
        cw = offscreen->width;
        ch = offscreen->height;
    } else {
        const u32 num = std::max(1u, settings.previewNumerator);
        const u32 den = std::max(1u, settings.previewDenominator);
        cw = std::max(1u, snap.compWidth * num / den);
        ch = std::max(1u, snap.compHeight * num / den);
        const u32 maxTex = backend_->capabilities().maxTexture2D;
        if (cw > maxTex || ch > maxTex) {
            const f32 r = static_cast<f32>(maxTex) / static_cast<f32>(std::max(cw, ch));
            cw = std::max(1u, static_cast<u32>(static_cast<f32>(cw) * r));
            ch = std::max(1u, static_cast<u32>(static_cast<f32>(ch) * r));
        }
    }
    TextureDesc compDesc;
    compDesc.width = cw;
    compDesc.height = ch;
    compDesc.format = kWorkFormat;
    compDesc.sampled = true;
    compDesc.renderTarget = true;
    compDesc.transferSrc = true;
    // Quadro guardado da prévia: RGBA8 sRGB (4 B/px, metade do RGBA16F). A
    // saída já limita a [0,1] e codifica em sRGB, então o quadro em memória é o
    // mesmo que a tela mostraria; a composição continua em 16F linear.
    TextureDesc cacheDesc = compDesc;
    cacheDesc.format = kPreviewCacheFormat;
    PreviewFrame* cached = nullptr;
    previewCacheHit_ = false;
    const bool usePreviewCache = !settings.cachedComposition.valid()
        && (!offscreen || offscreen->display.valid()) && !settings.finalQuality && !settings.rawPlayback
        && settings.previewCacheRevision && previewCacheCapacity_;
    if (usePreviewCache) {
        for (auto& f : previewFrames_) if (f.time == snap.time.value) { cached = &f; break; }
        if (!cached && previewFrames_.size() >= previewCacheCapacity_) {
            cached = &*std::min_element(previewFrames_.begin(), previewFrames_.end(),
                [](const auto& a, const auto& b) { return a.used < b.used; });
            // Poll; never stall playback or allocate replacements above the
            // budget while a prior submission is still reading this slot.
            if (!backend_->wait_frame(cached->gpuFrame, 0).ok()) cached = nullptr;
            else {
                cached->complete = false; cached->time = -1;
                publish_preview_buffer_ranges();
            }
        }
        if (!cached && previewFrames_.size() < previewCacheCapacity_) {
            previewFrames_.push_back({}); cached = &previewFrames_.back();
        }
        if (cached && !cached->texture.valid()) {
            auto texture = backend_->create_texture(cacheDesc);
            if (texture.ok()) cached->texture = *texture;
        }
        if (cached) {
            cached->time = snap.time.value;
            cached->used = ++previewCacheUse_;
            cached->gpuFrame = fb.frameNumber;
            previewCacheHit_ = cached->complete && cached->texture.valid();
            if (!cached->texture.valid()) cached = nullptr;
        }
    }
    const FGTexture cacheTex = cached ? graph_.import_texture("preview-cache", cached->texture, cacheDesc) : FGTexture{};
    if (settings.cachedComposition.valid() && settings.cachedCompositionPixels) {
        // Inside the opened frame: backend copies CPU bytes into its ordinary
        // upload stream. No synchronous submit/readback on the UI/display path.
        const Status uploaded = backend_->upload_texture(settings.cachedComposition, settings.cachedCompositionPixels, cw * 8);
        if (!uploaded.ok()) {
            pool_.end_frame();
            (void)backend_->discard_frame();
            snap.release_video_frames(); return uploaded;
        }
    }
    const FGTexture comp = settings.cachedComposition.valid()
                         ? graph_.import_texture("prerender-540p", settings.cachedComposition, compDesc)
                         : previewCacheHit_ ? cacheTex
                         : (offscreen && offscreen->texture.valid())
                         ? graph_.import_texture("composicao", offscreen->texture, compDesc)
                         : graph_.create_texture("composicao", compDesc);
    Status previewSourceStatus = OkStatus;
    currentScenes_ = &snap.scenes;
    if (offscreen && !snap.scenes.empty()) scene3d_.finish_environment(snap.scenes[0].environment);
    capturePhases.mark("scene-environment");
    compTargetW_ = cw;
    compTargetH_ = ch;

    if (previewCacheHit_ || settings.cachedComposition.valid()) {
        // The complete composition already contains text, effects and 3D.
        // Only the ordinary output pass (zoom, pan, display transform) remains.
    } else if (settings.rawPlayback) {
        const bool decoded = !snap.layers.empty() && build_video_source(snap.layers.front(), 0, cw, ch, comp, fb.frameNumber);
        if (decoded && snap.layers.front().source.frame->hardwareBuffer)
            framesInFlight_.push_back(snap.layers.front().source.frame);
        if (!decoded) {
            // Missing RAW pixels are not a successful black video frame. Keep
            // the last visible picture on backends that can discard safely,
            // and let Engine retry without advancing presentation telemetry.
            mark_incomplete(__FILE__, __LINE__);
            previewSourceStatus = Status{Errc::DecodeFailed, "frame RAW indisponivel para GPU"};
        }
    } else {
    upload_glyphs(snap);
    upload_masks(snap);
    upload_vectors(snap);
    upload_particle_history(snap);
    renderFrameNumber_ = fb.frameNumber;   // o relógio dos buffers extras das partículas (2D e cena)
    compose_layers(snap, comp, compDesc, fb.frameNumber, draws_, 0);
    }
    if (!offscreenFrame && fb.backbuffer.valid() && !previewCacheHit_ && videoSourceFailed_) {
        // A missing main source must not remove a video layer for one display
        // tick. Auxiliary temporal samples retain their existing valid-source
        // fallback; this applies only to absent/unusable main video pixels.
        mark_incomplete(__FILE__, __LINE__);
        previewSourceStatus = Status{Errc::DecodeFailed, "pixels principais do video indisponiveis"};
    }
    // A raiz de novo (as pré-composições trocaram o estado em curso).
    currentScenes_ = &snap.scenes;
    currentSnap_ = &snap;
    compTargetW_ = cw;
    compTargetH_ = ch;
    EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat,
                           std::min<u32>(backend_->capabilities().maxTexture2D, 8192));
    if (cached && !previewCacheHit_) {
        // Composição nova: uma cópia para o quadro guardado (RGBA8 sRGB).
        const Vec4 copyMap{1.0f, 1.0f, 0.0f, 0.0f};
        const u32 copy = ctx.fullscreen_pass("previa-cache", PassStage::Output, cacheTex, ShaderId::common_copy_frag,
                                             {PassTexture{comp, {}, CommonSampler::NearestClamp}},
                                             &copyMap, sizeof(copyMap));
        if (copy == kInvalidIndex) { cached->complete = false; cached->time = -1; cached = nullptr; }
        else graph_.mark_side_effect(copy);   // ninguém lê o slot neste quadro
    }

    // --- Saída.
    // Teste do passe de saída sem swapchain (OffscreenTarget::display): o
    // alvo, o tamanho e a rotação vêm do alvo; tudo o mais é o caminho da tela.
    const bool displayTest = offscreen && offscreen->display.valid() && !offscreen->yPlane.valid();
    if ((!offscreen && !settings.previewCacheOnly && fb.backbuffer.valid()) || displayTest) {
        const TextureHandle bbTex = displayTest ? offscreen->display : fb.backbuffer;
        const u32 bbW = displayTest ? offscreen->displayWidth : fb.backbufferWidth;
        const u32 bbH = displayTest ? offscreen->displayHeight : fb.backbufferHeight;
        const SurfaceFormat bbFormat = displayTest ? backend_->texture_desc(offscreen->display).format : fb.backbufferFormat;
        const SurfaceRotation rotation = displayTest ? offscreen->displayRotation : fb.rotation;
        TextureDesc bbDesc;
        bbDesc.width = bbW;
        bbDesc.height = bbH;
        bbDesc.format = bbFormat;
        const FGTexture bb = graph_.import_texture("swapchain", bbTex, bbDesc);
        auto pOut = shaders_.pipeline(PipelineKey::graphics(ShaderId::composite_layer_vert,
                                                            ShaderId::composite_output_frag, bbFormat));
        if (!pOut.ok()) {
            mark_incomplete(__FILE__, __LINE__);
            previewSourceStatus = pOut.status();
        }
        // A composição encaixada (letterbox) no espaço LÓGICO do display, e a
        // pré-rotação aplicada no clip: o compositor do sistema não precisa
        // girar a imagem (o que custaria um passe a mais por frame, dele).
        const bool swap = rotation == SurfaceRotation::Rotate90 || rotation == SurfaceRotation::Rotate270;
        const f32 dispW = static_cast<f32>(swap ? bbH : bbW);
        const f32 dispH = static_cast<f32>(swap ? bbW : bbH);
        const f32 compW = static_cast<f32>(snap.compWidth), compH = static_cast<f32>(snap.compHeight);
        const f32 fit = std::min(dispW / compW, dispH / compH) * std::max(0.01f, settings.viewportZoom);
        const f32 ox = (dispW - compW * fit) * 0.5f + settings.viewportPan.x;
        const f32 oy = (dispH - compH * fit) * 0.5f + settings.viewportPan.y;
        Mat4 dispFromComp = Mat4::translation(Vec3{ox, oy, 0}) * Mat4::scale(Vec3{fit, fit, 1});
        Mat4 clip = clip_from_comp(dispW, dispH) * dispFromComp;
        f32 angle = 0.0f;
        switch (rotation) {
            case SurfaceRotation::Rotate90:  angle = 90.0f; break;
            case SurfaceRotation::Rotate180: angle = 180.0f; break;
            case SurfaceRotation::Rotate270: angle = 270.0f; break;
            default: break;
        }
        if (angle != 0.0f) clip = Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, angle * kDeg2Rad)) * clip;

        const Vec4 bg = settings.editorBackground;
        void* ubo = arena_.alloc(16, 16);
        std::memcpy(ubo, &bg, 16);
        // Largura da composição em pixels do display: o shader de saída troca a
        // amostra única por uma média de área quando a imagem encolhe (zoom
        // pequeno, preview em tela menor que o alvo) — sem cintilar.
        // Fundo transparente: xadrez atrás da composição (só na tela; o export
        // lê a composição com alfa). Casas de ~1/40 do lado menor do display.
        const f32 checker = snap.background.w < 0.5f
            ? std::max(6.0f, std::round(std::min(dispW, dispH) / 40.0f)) : 0.0f;
        struct Cap { PipelineHandle p; FGTexture comp; u64 sampler; Mat4 clip; f32 w, h; f32 dither; void* ubo; f32 dispW; f32 checker; }
            cap{pOut.ok() ? *pOut : PipelineHandle{}, comp, shaders_.sampler(CommonSampler::LinearClamp).id,
                clip, compW, compH, settings.dither ? 1.0f : 0.0f, ubo, compW * fit, checker};
        const Vec4 clear = settings.pasteboard;
        const u32 pass = graph_.add_raster_pass("saida", PassStage::Output, bb, LoadOp::Clear, clear,
                                                [cap](PassContext& pc) {
            if (!cap.p.valid()) return;
            pc.cmds.bind_pipeline(cap.p);
            LayerPush push;
            push.clipFromLayer = cap.clip;
            push.region = Vec4{0.0f, 0.0f, cap.w, cap.h};
            push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
            const f32 texelsPerPixel = static_cast<f32>(pc.desc(cap.comp).width) / std::max(1.0f, cap.dispW);
            push.params = Vec4{cap.dither, texelsPerPixel, cap.checker, 0};
            pc.cmds.bind_texture(0, pc.texture(cap.comp), SamplerHandle{cap.sampler});
            pc.cmds.set_uniforms(cap.ubo, 16);
            pc.cmds.push_constants(&push, sizeof(push));
            pc.cmds.draw(6);
        });
        graph_.read(pass, comp);
        // O alvo de teste é uma textura comum (não há apresentação): fica
        // legível para o `read_texture` de quem chamou.
        graph_.set_output(bb, displayTest ? ResourceState::ShaderRead : ResourceState::Present);
    } else if (offscreen && offscreen->yPlane.valid() && offscreen->uvPlane.valid()) {
        // Export: composição → NV12 na GPU (o encoder recebe planos prontos).
        // Com upscale, antes: a composição (cw×ch) vira o tamanho de saída num
        // passe só (textureNicest + TAA modo 4 opcional), e o NV12 lê dela.
        const u32 ow = offscreen->planeWidth ? offscreen->planeWidth : cw;
        const u32 oh = offscreen->planeHeight ? offscreen->planeHeight : ch;
        FGTexture yuvSource = comp;
        if (offscreen->upscaled.valid() && (ow != cw || oh != ch)) {
            TextureDesc ad;
            ad.width = ow; ad.height = oh;
            ad.format = SurfaceFormat::RGBA16F;
            ad.sampled = true; ad.renderTarget = true;
            const FGTexture up = graph_.import_texture("export-upscale", offscreen->upscaled, ad);
            const bool history = offscreen->upscaleTemporal && offscreen->upscaleHistoryValid && offscreen->upscaleHistory.valid();
            const FGTexture prev = history ? graph_.import_texture("export-upscale-historico", offscreen->upscaleHistory, ad) : comp;
            struct UpscaleUniforms { Vec4 res; Vec4 cfg; } uu{
                Vec4{static_cast<f32>(cw), static_cast<f32>(ch), 1.0f / static_cast<f32>(cw), 1.0f / static_cast<f32>(ch)},
                Vec4{offscreen->upscaleTemporal ? 1.0f : 0.0f, history ? 1.0f : 0.0f, 30.0f, 0.0f}};
            const u32 pu = ctx.fullscreen_pass("export-upscale", PassStage::Output, up, ShaderId::export_upscale_nicest_frag,
                {PassTexture{comp, {}, CommonSampler::LinearClamp}, PassTexture{prev, {}, CommonSampler::LinearClamp}},
                &uu, sizeof(uu));
            if (pu != kInvalidIndex) { graph_.mark_side_effect(pu); yuvSource = up; }
        }
        // Empacotado: 4 bytes por fragmento num RGBA8 de 1/4 da largura (a
        // memória é a mesma do R8/RG8; o custo do passe era por fragmento).
        const bool packed = offscreen->packedYuv && (ow % 4u) == 0;
        TextureDesc yd;
        yd.width = packed ? ow / 4 : ow;
        yd.height = oh;
        yd.format = packed ? SurfaceFormat::RGBA8 : SurfaceFormat::R8;
        yd.renderTarget = true;
        yd.transferSrc = true;
        TextureDesc uvd = yd;
        uvd.width = packed ? ow / 4 : ow / 2;
        uvd.height = oh / 2;
        uvd.format = packed ? SurfaceFormat::RGBA8 : SurfaceFormat::RG8;
        const ShaderId yuvShader = packed ? ShaderId::export_yuv_encode_packed_frag : ShaderId::export_yuv_encode_frag;
        const FGTexture yTex = graph_.import_texture("export-y", offscreen->yPlane, yd);
        const FGTexture uvTex = graph_.import_texture("export-cbcr", offscreen->uvPlane, uvd);
        struct YuvUniforms { Vec4 cfg; Vec4 matrix; };
        const f32 dither = offscreen->encodeDither ? 1.0f : 0.0f;
        const YuvUniforms uy{Vec4{0.0f, dither, 0, 0}, Vec4{0.2126f, 0.0722f, 0, 0}};
        const YuvUniforms uc{Vec4{1.0f, dither, 0, 0}, Vec4{0.2126f, 0.0722f, 0, 0}};
        // surface-direct confirmado no 1o pacote: só o R'G'B' (sem NV12 nem readback).
        const bool nv12 = !(offscreen->rgbOnly && offscreen->rgb.valid());
        if (nv12) {
            const u32 py = ctx.fullscreen_pass("export-y", PassStage::Output, yTex, yuvShader,
                                               {PassTexture{yuvSource, {}, CommonSampler::NearestClamp}}, &uy, sizeof(uy));
            const u32 pc = ctx.fullscreen_pass("export-cbcr", PassStage::Output, uvTex, yuvShader,
                                               {PassTexture{yuvSource, {}, CommonSampler::NearestClamp}}, &uc, sizeof(uc));
            if (py != kInvalidIndex) graph_.mark_side_effect(py);
            if (pc != kInvalidIndex) graph_.mark_side_effect(pc);
        }
        if (offscreen->rgb.valid()) {
            // surface-direct: o MESMO R'G'B' de 8 bits que o NV12 codifica, num
            // alvo com memória de AHardwareBuffer que a Surface do encoder lê.
            const TextureDesc have = backend_->texture_desc(offscreen->rgb);
            if (have.width == ow && have.height == oh) {
                TextureDesc rd;
                rd.width = ow; rd.height = oh;
                rd.format = SurfaceFormat::RGBA8;
                rd.sampled = true; rd.renderTarget = true;
                const FGTexture rgbTex = graph_.import_texture("export-rgb", offscreen->rgb, rd);
                const YuvUniforms ur{Vec4{0.0f, dither, 0, 0}, Vec4{0.2126f, 0.0722f, 0, 0}};
                const u32 pr = ctx.fullscreen_pass("export-rgb", PassStage::Output, rgbTex, ShaderId::export_rgb_encode_frag,
                    {PassTexture{yuvSource, {}, CommonSampler::NearestClamp}}, &ur, sizeof(ur));
                if (pr != kInvalidIndex) graph_.mark_side_effect(pr);
            }
        }
        graph_.set_output(comp, ResourceState::ShaderRead);
    } else {
        graph_.set_output(comp, ResourceState::ShaderRead);
    }

    RenderCompletion completion;
    completion.frame = fb;
    completion.exposureStats = exposureStats;
    completion.stagedStats = stagedStats;
    completion.cached = cached;
    completion.targets = staged.targets;
    completion.effectsBefore = effectsBypassedBefore;
    completion.began = tBegin;
    completion.buildFrame = fb.frameNumber;
    completion.width = cw; completion.height = ch;
    completion.offscreen = offscreenFrame;
    capturePhases.mark("graph-build");
    Status result = previewSourceStatus.ok() ? graph_.compile(pool_) : previewSourceStatus;
    capturePhases.mark("graph-compile");
    bool frameOpen = true;
    bool segmented = false;
    if (result.code() == Errc::OutOfDeviceMemory && settings.allowGraphSegments
        && !offscreen && !offscreenFrame && fb.backbuffer.valid()
        && backend_->can_discard_frame() && backend_->can_lease_presentable_frame()
        && !incomplete_ && !scene3d_.incomplete()
        && EffectGraph::bypassed_total() == effectsBypassedBefore) {
        const Status lease = backend_->begin_presentable_frame_lease();
        if (lease.ok()) {
            auto& pending = previewGraph_;
            pending.active = pending.initial = true;
            pending.snapshot = &snap;
            pending.settings = settings;
            pending.completion = completion;
            timings.gpuProfileFragmented = true;
            read_timings(timings);
            pending.timings = timings;
            stats = {};
            pending.started = monotonic_ns();
            pending.activeRecordNs = pending.started - tBegin;
            pending.exposureCount = static_cast<u32>(completed.textures.size());
            pending.exposureTextures = std::move(completed.storage);
            completed.textures = {};
            pending.stagedCount = static_cast<u32>(staged.targets.size());
            pending.stagedTargets = std::move(staged.storage);
            staged.targets = {};
            backend_->begin_external_image_scope();
            graph_.release(pool_);
            pool_.end_frame();
            const Status submitted = backend_->discard_frame();
            pending.waitingFrame = backend_->last_submitted_frame();
            if (!submitted.ok()) {
                cancel_preview_graph();
                return submitted;
            }
            // Ownership spans turns: do not release FrameRefs, reset the CPU
            // arena, publish a cache slot or count presentation before final.
            return Status{Errc::Busy, "preview graph continuation pending"};
        }
    }
    if (result.code() == Errc::OutOfDeviceMemory && settings.allowGraphSegments
        && offscreenFrame && offscreen && offscreen->texture.valid()) {
        result = render_segmented_graph(settings, fb, timings, frameOpen);
        segmented = result.ok();
        if (cached) cached->gpuFrame = fb.frameNumber;
    }
    if (result.ok()) {
        if (!segmented) graph_.execute(*fb.commands, settings.gpuTimers);
        // Export: os planos NV12 vão para os buffers de leitura no mesmo frame.
        if (offscreen && offscreen->yReadback.valid() && offscreen->uvReadback.valid()
            && offscreen->yPlane.valid() && offscreen->uvPlane.valid()
            && !(offscreen->rgbOnly && offscreen->rgb.valid())) {
            fb.commands->end_render_pass();
            fb.commands->copy_texture_to_buffer(offscreen->yPlane, offscreen->yReadback);
            fb.commands->copy_texture_to_buffer(offscreen->uvPlane, offscreen->uvReadback);
        }
    } else {
        AUREA_LOG_ERROR("FrameGraph nao compilou: %s", result.message().data());
    }
    capturePhases.mark("graph-execute");
    completion.frame = fb;
    const Status finished = finish_render(snap, settings, offscreen, result, frameOpen, completion, stats, timings);
    capturePhases.mark("submit");
    return finished;
}

Status Renderer::finish_render(FrameSnapshot& snap, const RenderSettings& settings,
    const OffscreenTarget* offscreen, Status result, bool frameOpen, RenderCompletion& completion,
    FrameStats& stats, RenderTimings& timings) noexcept {
    graph_.release(pool_);
    pool_.end_frame();

    const u64 tRecorded = monotonic_ns();
    const bool discardPresentation = !result.ok() && !completion.offscreen
        && completion.frame.backbuffer.valid() && backend_->can_discard_frame();
    const Status endStatus = frameOpen ? (discardPresentation ? backend_->discard_frame() : backend_->end_frame()) : OkStatus;
    if (frameOpen && endStatus.ok() && completion.offscreen && settings.offscreenFenceSubmitted)
        settings.offscreenFenceSubmitted(settings.offscreenFenceContext, completion.frame.frameNumber);
    if (result.code() == Errc::OutOfDeviceMemory && endStatus.ok())
        failedGraphFrame_ = backend_->last_submitted_frame();
    if (!endStatus.ok()) for (auto& target : completion.targets)
        if (target.texture.valid()) target.pendingFrame = completion.frame.frameNumber;
    // TimeSlice com faixa aproximada: parado (e no scrub, no preparo do buffer,
    // na captura), o quadro fica INCOMPLETO — o render seguinte pede a próxima
    // faixa ao decoder. Antes ele saía "pronto": o Engine não redesenhava
    // quando a faixa chegava, a prévia parada ficava com as aproximadas e o
    // play começava sem as faixas de trás. Tocando, passa sem redesenho a cada
    // quadro decodificado. Nos dois casos não entra no cache como pronto.
    if (sliceApprox_ && !settings.previewPlaying) mark_incomplete(__FILE__, __LINE__);
    if (!incomplete_ && scene3d_.incomplete()) {
        incompleteFile_ = scene3d_.incomplete_file(); incompleteLine_ = scene3d_.incomplete_line();
    } else if (!incomplete_ && EffectGraph::bypassed_total() != completion.effectsBefore) {
        incompleteFile_ = "efeito ignorado (EffectGraph)"; incompleteLine_ = 0;
    }
    incomplete_ = incomplete_ || scene3d_.incomplete()
        || EffectGraph::bypassed_total() != completion.effectsBefore;
    if (completion.cached) completion.cached->complete = result.ok() && endStatus.ok() && !incomplete_
        && !sliceApprox_ && !snap.missingVideoFrames && !snap.staleVideoFrames
        && EffectGraph::bypassed_total() == completion.effectsBefore
        && !scene3d_.incomplete()
        && (snap.scenes.empty() || !environment_pending());
    if (completion.cached) publish_preview_buffer_ranges();
    if (!uploads_.empty()) {
        // Intermediate image/flow caches built from an unavailable upload are
        // not reusable after that upload succeeds. Destruction is GPU-deferred.
        for (auto& [key, image] : images_) destroy_image_linear(image);
        for (auto& [key, flow] : flowCache_)
            for (auto texture : flow.tex) if (texture.valid()) backend_->destroy_texture(texture);
        flowCache_.clear();
        heavyStats_.flowCacheBytes = 0;
    }
    const u64 tEnd = monotonic_ns();

    // External images remain owned by the decoder until GPU completion. CPU
    // planes have already been copied into backend staging by upload_texture;
    // keeping those ImageReader buffers across GPU frames starves temporal effects.
    for (FrameRef& fr : framesInFlight_) {
        DecodedFrame* raw = fr.detach();
        if (raw) backend_->defer_until_gpu_done([](void* p) { static_cast<DecodedFrame*>(p)->release(); }, raw);
    }
    framesInFlight_.clear();
    snap.release_video_frames();

    collect_resources(completion.buildFrame);

    timings.videoUploadMs = videoUploadMs_;
    timings.cpuRecordMs = static_cast<f32>(static_cast<f64>(tRecorded - completion.began) * 1e-6) + timings.sceneExposureRecordMs;
    timings.presentMs = static_cast<f32>(static_cast<f64>(tEnd - tRecorded) * 1e-6);
    read_timings(timings);

    heavyStats_.lastSceneDrawCalls += completion.stagedStats.lastSceneDrawCalls;
    heavyStats_.lastSceneShadowDrawCalls += completion.stagedStats.lastSceneShadowDrawCalls;
    heavyStats_.lastSceneInstancedDraws += completion.stagedStats.lastSceneInstancedDraws;
    heavyStats_.lastSceneVisible += completion.stagedStats.lastSceneVisible;
    heavyStats_.lastSceneCulled += completion.stagedStats.lastSceneCulled;
    heavyStats_.lastSceneTriangles += completion.stagedStats.lastSceneTriangles;
    heavyStats_.lastShadowMapSize = std::max(heavyStats_.lastShadowMapSize, completion.stagedStats.lastShadowMapSize);

    stats.passesExecuted = graph_.stats().passesExecuted + completion.exposureStats.passesExecuted;
    stats.passesCulled = graph_.stats().passesCulled + completion.exposureStats.passesCulled;
    stats.drawCalls = static_cast<u32>(draws_.size()) + graph_.stats().passesExecuted + completion.exposureStats.drawCalls;
    stats.layersRendered = static_cast<u32>(draws_.size());
    stats.previewWidth = completion.width;
    stats.previewHeight = completion.height;
    stats.gpuMs = timings.gpuMeasured ? timings.gpuTotalMs : 0.0f;
    stats.gpuMemoryBytes = backend_->memory_stats().usedBytes;
    ++framesRendered_;

    if (!result.ok()) return result;
    return endStatus;
}

void Renderer::set_effect_preview_source(std::vector<u8> rgba, u32 width, u32 height) noexcept {
    if (previewGraph_.active) return;
    if (width == 0 || height == 0) {   // sem foto: volta para a cartela de teste
        if (previewSrcTex_.valid()) backend_->destroy_texture(previewSrcTex_);
        previewSrcTex_ = TextureHandle{};
        previewSrc_.clear();
        previewSrcW_ = previewSrcH_ = 0;
        previewSrcDirty_ = false;
        return;
    }
    if (rgba.size() < static_cast<usize>(width) * height * 4) return;
    if (previewSrcTex_.valid() && (width != previewSrcW_ || height != previewSrcH_)) {
        backend_->destroy_texture(previewSrcTex_);
        previewSrcTex_ = TextureHandle{};
    }
    previewSrc_ = std::move(rgba);
    previewSrcW_ = width;
    previewSrcH_ = height;
    previewSrcDirty_ = true;
}

Status Renderer::render_effect_preview(const EffectRegistry& effects, EffectTypeId type,
                                       u32 width, u32 height, std::vector<u8>& outRgba, bool comparison) noexcept {
    // A prévia estática de sempre: um quadro, foto (ou cartela), sem fonte da CPU.
    PreviewFrameSpec spec;
    spec.comparison = comparison;
    if (comparison) { spec.localTime = 18; spec.clipProgress = 0.5f; }
    return render_effect_preview_impl(effects, type, width, height, spec, outRgba);
}

Status Renderer::render_effect_preset_preview(const EffectRegistry& effects, EffectTypeId type, u32 width, u32 height,
                                              const u8* sourceRgba, const EffectPreset* preset,
                                              std::vector<u8>& outRgba) noexcept {
    if (!sourceRgba || !preset) return Status{Errc::InvalidArgument, "miniatura sem fonte ou preset"};
    PreviewFrameSpec spec;
    spec.source = sourceRgba;
    spec.preset = preset;
    return render_effect_preview_impl(effects, type, width, height, spec, outRgba);
}

Status Renderer::render_effect_preview_impl(const EffectRegistry& effects, EffectTypeId type, u32 width, u32 height,
                                            const PreviewFrameSpec& spec, std::vector<u8>& outRgba) noexcept {
    const bool comparison = spec.comparison;
    if (!backend_) return Status{Errc::InvalidState, "renderer sem backend"};
    if (previewGraph_.active) return Status{Errc::Busy, "preview graph continuation active"};
    if (const Status ready = reclaim_failed_transients(); !ready.ok()) return ready;
    if (width == 0 || height == 0) return Status{Errc::InvalidArgument, "previa sem tamanho"};
    const Effect* effect = effects.find(type);
    if (!effect) return Status{Errc::NotFound, "efeito desconhecido"};
    const ParameterRegistry* params = effects.params(type);
    if (!params) return Status{Errc::NotFound, "efeito sem parametros"};
    // Um quadro solto não diz nada sobre efeito que precisa de uma SEQUÊNCIA
    // (eco, remapeamento de tempo) nem de um histograma do quadro inteiro.
    // Não é falha: é um efeito que a cartela não representa, e a UI mostra a
    // cartela genérica em vez de uma imagem que mentiria. A SEQUÊNCIA animada
    // (render_effect_preview_sequence) representa: ela entra com o resultado
    // temporal já montado ou com o quadro passado (`spec.allowTemporal`).
    if (!spec.allowTemporal
        && (effect->effect_class() == EffectClass::Temporal || effect->effect_class() == EffectClass::Global)) {
        return Status{Errc::NotImplemented, "efeito sem previa de um quadro"};
    }

    // Fontes vindas da CPU (cenas procedurais da sequência, histórico, resultado
    // temporal): texturas nossas, destruídas em qualquer saída depois do quadro.
    struct OwnedTextures {
        GPUBackend* backend;
        TextureHandle handles[3]{};
        u32 count = 0;
        ~OwnedTextures() { for (u32 i = 0; i < count; ++i) if (handles[i].valid()) backend->destroy_texture(handles[i]); }
    } owned{backend_};
    // A cena escolhida não é a foto: o mapa de profundidade da foto não vale.
    struct DepthGate {
        bool& flag; bool saved;
        ~DepthGate() { flag = saved; }
    } depthGate{previewDepthFromPhoto_, previewDepthFromPhoto_};
    if (spec.source || spec.forcePlate) previewDepthFromPhoto_ = false;

    FrameBegin fb;
    // Offscreen: a cartela é renderizada e lida de volta, não vai para a tela.
    // Abrir um frame com superfície aqui apresentaria o quadro da PRÉVIA no
    // lugar do quadro da tela — e a prévia roda fora da thread de render.
    if (const Status s = backend_->begin_offscreen_frame(fb); !s.ok()) return s;

    // Preview de efeito NUNCA é rebaixado pelo calor: a cartela é 320×200 e
    // roda uma vez. O que o aparelho mostra tem de ser o que o efeito faz.
    heavyScale_ = 1.0f;
    quality_ = PreviewQuality{};
    heavyQ_ = HeavyQuality::full();

    pool_.begin_frame(*backend_, fb.frameNumber);
    graph_.reset();
    arena_.reset();
    draws_.clear();
    currentScenes_ = nullptr;
    currentSnap_ = nullptr;
    compTargetW_ = width;
    compTargetH_ = height;

    const u32 maxTex = std::min<u32>(backend_->capabilities().maxTexture2D, 8192);

    // --- A entrada: a cartela de demonstração, no formato de trabalho.
    TextureDesc inDesc;
    inDesc.width = width;
    inDesc.height = height;
    inDesc.format = kWorkFormat;
    inDesc.sampled = true;
    inDesc.renderTarget = true;
    auto plateDesc = [&]() noexcept {
        TextureDesc d = inDesc;
        d.debugName = "previa-do-efeito";
        return d;
    };
    const FGTexture plate = graph_.create_texture("cartela", inDesc);

    EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat, maxTex);
    // A foto de base (quando o app mandou uma): sobe uma vez, vira linear e é
    // recortada no formato pedido, CENTRALIZADA (a amostra de 0.2.4 é um
    // objeto no meio do quadro; o corte puxado para o rosto, 0,35, saiu junto
    // com a foto antiga). Mesma conta em RendererDepth.cpp (depth_map_preview).
    // Sem foto, a cartela de teste do shader.
    // Uma imagem RGBA8 sRGB (alfa reto) da CPU, do tamanho pedido, sobe e vira
    // linear no formato de trabalho — o mesmo passe da foto.
    auto upload_cpu = [&](const u8* rgba, const char* name, FGTexture target) noexcept -> bool {
        if (!rgba || owned.count >= 3) return false;
        TextureDesc d;
        d.width = width;
        d.height = height;
        d.format = SurfaceFormat::RGBA8;
        d.sampled = true;
        d.transferDst = true;
        d.debugName = name;
        auto t = backend_->create_texture(d);
        if (!t.ok()) return false;
        owned.handles[owned.count++] = *t;
        if (!backend_->upload_texture(*t, rgba, width * 4).ok()) return false;
        const Vec4 flags[2]{Vec4{0.0f, 0.0f, 0.0f, 0.0f}, Vec4{0.0f, 0.0f, 0.0f, 0.0f}};   // rgba_to_linear: flags + texel
        return ctx.fullscreen_pass(name, PassStage::Decode, target, ShaderId::video_rgba_to_linear_frag,
                                   {PassTexture{{}, *t, CommonSampler::LinearClamp}}, &flags, sizeof(flags)) != kInvalidIndex;
    };
    bool usedPhoto = false;
    if (spec.source) {
        usedPhoto = upload_cpu(spec.source, "cena-da-previa", plate);
        if (!usedPhoto) {
            graph_.release(pool_);
            pool_.end_frame();
            (void)backend_->end_frame();
            return Status{Errc::OutOfDeviceMemory, "cena da previa nao subiu"};
        }
    }
    if (!usedPhoto && !spec.forcePlate && previewSrcW_ > 0 && previewSrcH_ > 0) {
        if (!previewSrcTex_.valid()) {
            TextureDesc d;
            d.width = previewSrcW_;
            d.height = previewSrcH_;
            d.format = SurfaceFormat::RGBA8;
            d.sampled = true;
            d.transferDst = true;
            d.debugName = "foto-das-previas";
            auto t = backend_->create_texture(d);
            if (t.ok()) {
                previewSrcTex_ = *t;
                previewSrcDirty_ = true;
            }
        }
        if (previewSrcTex_.valid() && previewSrcDirty_) {
            previewSrcDirty_ = !backend_->upload_texture(previewSrcTex_, previewSrc_.data(), previewSrcW_ * 4).ok();
        }
        if (previewSrcTex_.valid() && !previewSrcDirty_) {
            TextureDesc ld = inDesc;
            ld.width = previewSrcW_;
            ld.height = previewSrcH_;
            const FGTexture lin = graph_.create_texture("foto-linear", ld);
            const Vec4 flags[2]{Vec4{0.0f, 0.0f, 0.0f, 0.0f}, Vec4{0.0f, 0.0f, 0.0f, 0.0f}};   // rgba_to_linear: flags + texel
            const f32 src = static_cast<f32>(previewSrcW_) / static_cast<f32>(previewSrcH_);
            const f32 dst = static_cast<f32>(width) / static_cast<f32>(height);
            Vec4 map{1.0f, 1.0f, 0.0f, 0.0f};   // uv = v_uv * xy + zw
            if (dst > src) {                     // mais largo: corta em cima/embaixo
                map.y = src / dst;
                map.w = (1.0f - map.y) * 0.5f;
            } else {                             // mais alto: corta dos lados
                map.x = dst / src;
                map.z = (1.0f - map.x) * 0.5f;
            }
            usedPhoto = ctx.fullscreen_pass("foto", PassStage::Decode, lin, ShaderId::video_rgba_to_linear_frag,
                                            {PassTexture{{}, previewSrcTex_, CommonSampler::LinearClamp}}, &flags, sizeof(flags)) != kInvalidIndex
                     && ctx.fullscreen_pass("foto-recorte", PassStage::Decode, plate, ShaderId::common_copy_frag,
                                            {PassTexture{lin, {}, CommonSampler::LinearClamp}}, &map, sizeof(map)) != kInvalidIndex;
        }
    }
    if (!usedPhoto) {
        const Vec4 size{static_cast<f32>(width), static_cast<f32>(height), 0.0f, 0.0f};
        if (ctx.fullscreen_pass("cartela", PassStage::Effects, plate, ShaderId::effects_preview_plate_frag,
                                {}, &size, sizeof(size)) == kInvalidIndex) {
            graph_.release(pool_);
            pool_.end_frame();
            (void)backend_->end_frame();
            return Status{Errc::PipelineCompileFailed, "cartela nao compilou"};
        }
    }

    // --- A instância: valores PADRÃO da declaração, um efeito, sem keyframes.
    EffectInstance instance;
    initialize_instance(instance, *params);
    std::vector<ParamValue> values(params->count());
    for (u32 i = 0; i < params->count(); ++i) values[i] = instance.params[i].constant;
    // A prévia mostra o efeito LIGADO: com os padrões de fábrica (quase sempre
    // neutros) a cartela sairia intacta e a prévia não diria nada. Miniatura
    // de look: os valores do preset no lugar dos de demonstração.
    if (spec.preset) {
        for (const EffectPresetValue& pv : spec.preset->values) {
            if (pv.param >= values.size() || !std::isfinite(pv.value)) continue;
            const ParamSpec& ps = params->at(pv.param);
            ParamValue& dst = values[pv.param];
            if (ps.type == ParamType::Bool) dst = ParamValue::boolean(pv.value >= 0.5f);
            else if (ps.type == ParamType::Int || ps.type == ParamType::Enum) dst.v[0] = std::round(pv.value);
            else dst.v[std::min<u32>(pv.component, 3u)] = pv.value;
        }
    } else {
        (void)effect->demo_values(instance, values);
    }
    // Sequência animada: os parâmetros de demonstração andam desde o padrão
    // (ver effect_preview_apply_sweep).
    if (std::isfinite(spec.sweep)) effect_preview_apply_sweep(*params, instance, values, spec.sweep);

    // A camada cobre o quadro inteiro, sem transformação: é o caso mais simples
    // e o que faz todo efeito de domínio (Motion Tile) se comportar.
    LayerPlacement placement;
    placement.compFromLayer = Mat4::identity();
    placement.compWidth = width;
    placement.compHeight = height;
    placement.layerWidth = width;
    placement.layerHeight = height;

    EffectEval eval;
    eval.effect = effect;
    eval.instance = &instance;
    eval.resources = this;
    eval.values = values.data();
    eval.valueOffset = 0;
    eval.count = static_cast<u32>(values.size());
    eval.effectIndex = 0;
    eval.localTime = FrameIndex{spec.localTime};
    eval.clipProgress = spec.clipProgress;
    eval.texelScale = 1.0f;
    eval.placement = &placement;
    // Recurso resolvido fora do grafo (o mapa de profundidade da foto das
    // prévias); sem camada nem projeto, os outros devolvem vazio.
    const usize previewUploadsFrom = uploads_.size();
    effect->resolve_resources(eval);

    const LayerImage input{plate, Rect{0.0f, 0.0f, static_cast<f32>(width), static_cast<f32>(height)}, width, height};
    LayerImage result;
    Status built = OkStatus;

    // Sequência: a captura da cena (sem efeito), o resultado temporal montado
    // fora (o eco, o RGB no tempo... somados na CPU sobre a cena que anda) e o
    // quadro PASSADO para quem compara com ele (Detectar movimento, Datamosh).
    if (spec.sourceOnly) {
        result = input;
    } else if (spec.result) {
        const FGTexture done = graph_.create_texture("resultado-temporal", inDesc);
        if (!upload_cpu(spec.result, "resultado-temporal", done)) {
            graph_.release(pool_);
            pool_.end_frame();
            (void)backend_->end_frame();
            return Status{Errc::OutOfDeviceMemory, "resultado temporal nao subiu"};
        }
        result = LayerImage{done, input.region, width, height};
    } else if (spec.history && effect->wants_history()) {
        const FGTexture past = graph_.create_texture("historico-da-previa", inDesc);
        if (upload_cpu(spec.history, "historico-da-previa", past)) {
            ctx.set_history(LayerImage{past, input.region, width, height});
        }
    }

    // EFEITO NEUTRO: a prévia é a própria cartela. Isto não é um atalho — é a
    // resposta certa para os controles de expressão (que existem no painel e
    // não desenham nada) e para qualquer efeito nos valores de demonstração
    // que não mudam a imagem. Sem esta linha, eles apareceriam como "sem
    // prévia", que para um efeito que não muda nada é simplesmente falso.
    if (!result.valid() && effect->is_identity(eval)) {
        const FGTexture same = graph_.create_texture("previa", plateDesc());
        const Vec4 copyMap{1.0f, 1.0f, 0.0f, 0.0f};
        if (ctx.fullscreen_pass("previa", PassStage::Output, same, ShaderId::common_copy_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &copyMap, sizeof(copyMap)) == kInvalidIndex) {
            graph_.release(pool_);
            pool_.end_frame();
            (void)backend_->end_frame();
            return Status{Errc::PipelineCompileFailed, "copia da cartela nao compilou"};
        }
        result.texture = same;
        result.region = input.region;
        result.width = width;
        result.height = height;
    }

    // Um efeito POR PIXEL não monta passe nenhum: ele devolve uma operação de
    // cor, e o grafo funde as operações vizinhas num passe só. Aqui não há
    // vizinhas — a operação roda sozinha no MESMO `color_stack.frag`, com a
    // mesma estrutura de uniforms. Sem isto, metade do catálogo (exposição,
    // saturação, níveis, curvas) não teria prévia.
    ColorOp single{};
    if (result.valid()) {
        // já resolvido acima: efeito neutro
    } else if (effect->color_op(eval, single)) {
        struct ColorStackUniforms {
            f32 header[4];
            f32 ops[kMaxFusedColorOps * 16];
        };
        static_assert(sizeof(ColorStackUniforms) == 16 + kMaxFusedColorOps * 64,
                      "layout std140 do color_stack.frag");
        ColorStackUniforms u{};
        u.header[0] = 1.0f;
        std::memcpy(u.ops, single.p, sizeof(single.p));
        u.ops[0] = static_cast<f32>(static_cast<u32>(single.code));
        TextureDesc fxDesc;
        fxDesc.width = width;
        fxDesc.height = height;
        fxDesc.format = kWorkFormat;
        fxDesc.sampled = true;
        fxDesc.renderTarget = true;
        result.texture = graph_.create_texture("efeito", fxDesc);
        result.region = input.region;
        result.width = width;
        result.height = height;
        if (ctx.fullscreen_pass("efeito", PassStage::Effects, result.texture, ShaderId::effects_color_stack_frag,
                                {PassTexture{input.texture, {}, CommonSampler::NearestClamp},
                                 PassTexture{{}, single.lut, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            built = Status{Errc::PipelineCompileFailed, "color stack nao compilou"};
            result = LayerImage{};
        }
    } else {
        built = effect->build(ctx, eval, input, 0.0f, result);
    }
    if (!built.ok() || !result.valid()) {
        graph_.release(pool_);
        pool_.end_frame();
        (void)backend_->end_frame();
        return built.ok() ? Status{Errc::NotImplemented, "efeito sem saida"} : built;
    }

    // --- O que sai na tela: a JANELA DO QUADRO lida da região de saída. Um
    // efeito de domínio devolve uma região maior (ou em outro lugar); sem este
    // passe a prévia mostraria o canto da textura em vez da imagem.
    //
    // A textura de saída é NOSSA, não do grafo: ela precisa sobreviver ao fim
    // do frame para ser lida de volta (o grafo devolveria a física ao pool).
    TextureDesc outDesc = inDesc;
    outDesc.sampled = true;
    outDesc.renderTarget = true;
    outDesc.transferSrc = true;
    outDesc.debugName = "previa-de-efeito";
    auto made = backend_->create_texture(outDesc);
    if (!made.ok()) {
        graph_.release(pool_);
        pool_.end_frame();
        (void)backend_->end_frame();
        return made.status();
    }
    const TextureHandle shownHandle = *made;
    const FGTexture shown = graph_.import_texture("previa", shownHandle, outDesc);
    {
        const Vec4 uvMap = EffectBuildContext::uv_map(Rect{0.0f, 0.0f, static_cast<f32>(width), static_cast<f32>(height)},
                                                      result.region);
        struct PreviewMap { Vec4 effectMap; Vec4 size; };
        const PreviewMap compare{uvMap, Vec4{static_cast<f32>(width), static_cast<f32>(height), 0, 0}};
        const auto copy = comparison
            ? ctx.fullscreen_pass("previa-comparada", PassStage::Output, shown, ShaderId::effects_preview_compare_frag,
                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}, PassTexture{result.texture, {}, CommonSampler::LinearClamp}}, &compare, sizeof(compare))
            : ctx.fullscreen_pass("previa", PassStage::Output, shown, ShaderId::common_copy_frag,
                {PassTexture{result.texture, {}, CommonSampler::LinearClamp}}, &uvMap, sizeof(uvMap));
        if (copy == kInvalidIndex) {
            graph_.release(pool_);
            pool_.end_frame();
            (void)backend_->end_frame();
            backend_->destroy_texture(shownHandle);
            return Status{Errc::PipelineCompileFailed, "copia da previa nao compilou"};
        }
    }
    graph_.set_output(shown, ResourceState::TransferSrc);

    // Textura de DADO pedida no planejamento DESTA prévia (data_texture: a
    // Constelação, a paleta do Quantizar cores): fora do laço de render
    // ninguém a mandaria para a GPU antes do passe que a lê, e a prévia sairia
    // com a textura vazia. Sobe aqui, no frame aberto, antes dos passes; só os
    // pedidos novos — os da composição seguem para o próximo quadro.
    {
        usize keep = std::min(previewUploadsFrom, uploads_.size());
        for (usize i = keep; i < uploads_.size(); ++i) {
            PendingUpload& up = uploads_[i];
            if (backend_->upload_texture(up.texture, up.data.data(), up.bytesPerRow).ok()) continue;
            if (keep != i) uploads_[keep] = std::move(up);
            ++keep;
        }
        uploads_.resize(keep);
    }

    const Status compiled = graph_.compile(pool_);
    if (compiled.ok()) {
        graph_.execute(*fb.commands, false);
    } else {
        AUREA_LOG_ERROR("previa de efeito nao compilou: %s", compiled.message().data());
    }
    graph_.release(pool_);
    pool_.end_frame();
    const Status ended = backend_->end_frame();
    if (compiled.code() == Errc::OutOfDeviceMemory && ended.ok())
        failedGraphFrame_ = backend_->last_submitted_frame();
    if (!compiled.ok() || !ended.ok()) {
        backend_->destroy_texture(shownHandle);
        return compiled.ok() ? ended : compiled;
    }

    // --- Leitura de volta: RGBA16F pré-multiplicado → RGBA8 sRGB de alfa reto.
    std::vector<u16> half(static_cast<usize>(width) * height * 4);
    const Status read = backend_->read_texture(shownHandle, half.data(), width * 8);
    backend_->destroy_texture(shownHandle);
    if (!read.ok()) return read;

    auto h2f = [](u16 h) noexcept {
        const u32 sign = static_cast<u32>(h & 0x8000u) << 16;
        const u32 exp = (h >> 10) & 0x1Fu;
        const u32 mant = h & 0x3FFu;
        u32 bits;
        if (exp == 0) {
            if (mant == 0) {
                bits = sign;
            } else {
                u32 e = 113, m = mant;
                while (!(m & 0x400u)) { m <<= 1; --e; }
                bits = sign | (e << 23) | ((m & 0x3FFu) << 13);
            }
        } else if (exp == 31) {
            bits = sign | 0x7F800000u | (mant << 13);
        } else {
            bits = sign | ((exp + 112u) << 23) | (mant << 13);
        }
        f32 f;
        std::memcpy(&f, &bits, 4);
        return f;
    };
    auto encode = [](f32 v) noexcept {
        const f32 c = std::clamp(v, 0.0f, 1.0f);
        const f32 e = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
        return static_cast<u8>(std::lround(std::clamp(e, 0.0f, 1.0f) * 255.0f));
    };
    outRgba.resize(static_cast<usize>(width) * height * 4);
    for (usize i = 0; i < static_cast<usize>(width) * height; ++i) {
        const f32 a = h2f(half[i * 4 + 3]);
        const f32 inv = a > 1e-5f ? 1.0f / a : 0.0f;   // o trabalho é pré-multiplicado
        outRgba[i * 4 + 0] = encode(h2f(half[i * 4 + 0]) * inv);
        outRgba[i * 4 + 1] = encode(h2f(half[i * 4 + 1]) * inv);
        outRgba[i * 4 + 2] = encode(h2f(half[i * 4 + 2]) * inv);
        outRgba[i * 4 + 3] = static_cast<u8>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f));
    }
    return OkStatus;
}

bool Renderer::has_slice_texture(u64 layer, i64 us) const noexcept {
    std::lock_guard<std::mutex> sl(sliceMutex_);
    for (const SliceTexture& s : sliceCache_) if (s.layer == layer && s.us == us && s.tex.valid()) return true;
    return false;
}

void Renderer::release_slice_cache(bool destroy) noexcept {
    std::lock_guard<std::mutex> sl(sliceMutex_);
    if (destroy && backend_) for (SliceTexture& s : sliceCache_) if (s.tex.valid()) backend_->destroy_texture(s.tex);
    sliceCache_.clear();
}

// =============================================================================
// APRIMORAR: o vizinho no tempo de uma camada de vídeo. O quadro exato da
// fonte é convertido UMA vez para uma textura persistente no cache das faixas
// (chave própria: camada + sal, para não brigar com o TimeSlice da mesma
// camada) e reaproveitado enquanto estiver na janela dos vizinhos: tocando,
// cada quadro converte só o vizinho novo da frente. O que saiu da janela (não
// usado há 2 quadros) é solto na hora — a memória fica em ~7 quadros por camada.
// =============================================================================
FGTexture Renderer::neighbor_video_texture(const RenderLayer& layer, u32 layerIndex, const RenderLayer::NeighborSample& n,
                                           u32 slot, u32 w, u32 h, u64 frameNumber) noexcept {
    if (n.sourceUs < 0 || !backend_) return FGTexture{};
    TextureDesc sd;
    sd.width = w;
    sd.height = h;
    sd.format = kWorkFormat;
    sd.sampled = true;
    sd.renderTarget = true;
    sd.debugName = "aprimorar-vizinho";
    const u64 key = layer.id.pack() ^ kNeighborSliceSalt;
    FGTexture image{};
    bool convert = false;
    bool resample = false;
    {
        std::lock_guard<std::mutex> sl(sliceMutex_);
        SliceTexture* entry = nullptr;
        for (SliceTexture& e : sliceCache_) {
            if (e.layer == key && e.us == n.sourceUs && e.tex.valid()) { entry = &e; break; }
        }
        if (!entry && n.frame) {
            auto t = backend_->create_texture(sd);
            if (!t.ok()) return FGTexture{};
            sliceCache_.push_back(SliceTexture{});
            entry = &sliceCache_.back();
            entry->layer = key;
            entry->us = n.sourceUs;
            entry->tex = *t;
            entry->w = w;
            entry->h = h;
            convert = true;
        }
        if (!entry) return FGTexture{};
        entry->lastFrame = frameNumber;
        if (convert) {
            image = graph_.import_texture("aprimorar-vizinho", entry->tex, sd);
            entry->fg = image;
            entry->fgFrame = frameNumber;
            entry->created = frameNumber;
            sliceGraphFrame_ = frameNumber;
        } else if (entry->fgFrame == frameNumber) {
            image = entry->fg;
            resample = entry->w != w || entry->h != h;
        } else {
            TextureDesc ed = sd;
            ed.width = entry->w;
            ed.height = entry->h;
            image = graph_.import_texture("aprimorar-vizinho", entry->tex, ed);
            entry->fg = image;
            entry->fgFrame = frameNumber;
            resample = entry->w != w || entry->h != h;
        }
    }
    if (convert) {
        sliceSalt_ = 0x5E1A0000000ull + slot;   // planos de CPU próprios por vizinho
        const bool built = build_video_source(layer, layerIndex, w, h, image, frameNumber, n.frame.get());
        sliceSalt_ = 0;
        if (!built) {
            // Conversão que não gravou: a entrada não pode ficar como pronta.
            std::lock_guard<std::mutex> sl(sliceMutex_);
            for (auto it = sliceCache_.begin(); it != sliceCache_.end(); ++it) {
                if (it->layer != key || it->us != n.sourceUs) continue;
                if (it->tex.valid()) backend_->destroy_texture(it->tex);
                sliceCache_.erase(it);
                break;
            }
            return FGTexture{};
        }
        if (n.frame->hardwareBuffer) framesInFlight_.push_back(n.frame);
    }
    if (resample) {
        // A densidade mudou (AUTO, zoom): o instante certo noutra resolução.
        const FGTexture stored = image;
        const Vec4 copyMap{1.0f, 1.0f, 0.0f, 0.0f};
        EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat, std::min<u32>(backend_->capabilities().maxTexture2D, 8192));
        image = graph_.create_texture("aprimorar-vizinho-reamostrado", sd);
        if (ctx.fullscreen_pass("aprimorar-vizinho-reamostrado", PassStage::Decode, image, ShaderId::common_copy_frag,
                                {PassTexture{stored, {}, CommonSampler::LinearClamp}}, &copyMap, sizeof(copyMap)) == kInvalidIndex)
            return FGTexture{};
    }
    return image;
}

// =============================================================================
// FLOW RETIME, "Substituir quadros duplicados": a assinatura de cada quadro da
// fonte — 32×18 células, cada uma a soma de 2×2 amostras de luma — vem dos
// planos de CPU do quadro uma vez só (no decode por hardware sem planos, uma
// cópia para a CPU por quadro-fonte) e fica guardada por asset + instante
// (até 512 por asset, sai a menos usada). Comparar dois quadros é comparar
// 576 números: nada na GPU, nada esperando.
// =============================================================================
const Renderer::FrameSignature* Renderer::find_signature(u64 asset, i64 pts) noexcept {
    const auto track = signatures_.find(asset);
    if (track == signatures_.end()) return nullptr;
    const auto it = track->second.frames.find(pts);
    if (it == track->second.frames.end()) return nullptr;
    it->second.used = ++track->second.clock;
    return &it->second;
}

const Renderer::FrameSignature* Renderer::frame_signature(u64 asset, i64 pts, DecodedFrame* f) noexcept {
    if (const FrameSignature* s = find_signature(asset, pts)) return s;
    if (!f || f->width == 0 || f->height == 0) return nullptr;
    if ((!f->planes[0] || f->planeCount == 0) && !f->prepare_cpu_planes()) return nullptr;
    if (!f->planes[0] || f->strides[0] == 0) return nullptr;
    const bool rgba = f->format == PixelFormat::RGBA8;
    const bool tenBit = f->format == PixelFormat::P010;
    const bool yuv = f->format == PixelFormat::NV12 || f->format == PixelFormat::NV21
                  || f->format == PixelFormat::YUV420P || tenBit;
    if (!rgba && !yuv) return nullptr;
    // RGBA da cópia para a CPU já vem só com a área visível.
    const u32 x0 = rgba ? 0u : f->cropLeft, y0 = rgba ? 0u : f->cropTop;
    const u32 vw = !rgba && f->visibleWidth ? std::min(f->visibleWidth, f->width - std::min(x0, f->width)) : f->width;
    const u32 vh = !rgba && f->visibleHeight ? std::min(f->visibleHeight, f->height - std::min(y0, f->height)) : f->height;
    if (vw == 0 || vh == 0) return nullptr;
    const u8* plane = f->planes[0];
    const usize stride = f->strides[0];
    auto luma = [&](u32 x, u32 y) -> u32 {
        x = std::min(x, vw - 1) + x0;
        y = std::min(y, vh - 1) + y0;
        const u8* row = plane + static_cast<usize>(y) * stride;
        if (rgba) {
            const u8* p = row + static_cast<usize>(x) * 4;
            return (54u * p[0] + 183u * p[1] + 19u * p[2]) >> 8;
        }
        if (tenBit) return row[static_cast<usize>(x) * 2 + 1];   // byte alto do P010
        return row[x];
    };
    FrameSignature s;
    s.pts = pts;
    for (u32 cy = 0; cy < 18; ++cy) {
        for (u32 cx = 0; cx < 32; ++cx) {
            const u32 px = static_cast<u32>((static_cast<u64>(cx) * 2 + 1) * vw / 64);
            const u32 py = static_cast<u32>((static_cast<u64>(cy) * 2 + 1) * vh / 36);
            const u32 qx = px > 0 ? px - 1 : 0, qy = py > 0 ? py - 1 : 0;
            s.luma[cy * 32 + cx] = static_cast<u16>(luma(qx, qy) + luma(px, qy) + luma(qx, py) + luma(px, py));
        }
    }
    SignatureTrack& track = signatures_[asset];
    if (track.frames.size() >= 512) {
        auto oldest = track.frames.begin();
        for (auto it = track.frames.begin(); it != track.frames.end(); ++it)
            if (it->second.used < oldest->second.used) oldest = it;
        track.frames.erase(oldest);
    }
    s.used = ++track.clock;
    auto [it, inserted] = track.frames.insert_or_assign(pts, s);
    (void)inserted;
    return &it->second;
}

bool Renderer::signatures_match(const FrameSignature& a, const FrameSignature& b, f32 sensitivity) noexcept {
    // Em quartos de código: a média das diferenças e as células que mudaram de
    // verdade (> 6 códigos — um objeto pequeno andando num quadro parado).
    u32 sum = 0, moved = 0;
    for (u32 i = 0; i < kSignatureCells; ++i) {
        const u32 d = static_cast<u32>(std::abs(static_cast<i32>(a.luma[i]) - static_cast<i32>(b.luma[i])));
        sum += d;
        moved += d > 24u ? 1u : 0u;
    }
    const f32 s = std::clamp(std::isfinite(sensitivity) ? sensitivity : 10.0f, 0.0f, 100.0f);
    const f32 mad = static_cast<f32>(sum) / (4.0f * static_cast<f32>(kSignatureCells));   // códigos
    return mad <= 0.15f + 0.05f * s
        && static_cast<f32>(moved) <= static_cast<f32>(kSignatureCells) * (0.004f + 0.0006f * s);
}

void Renderer::collect_resources(u64 frameNumber) noexcept {
    // Recursos persistentes de layers que sumiram. Checado a cada 2 s — não
    // há pressa, e varrer mapas a cada frame é custo sem ganho.
    if (frameNumber % 120 != 0) return;
    {
        // TimeSlice: faixas de instantes que não aparecem há 4 s.
        std::lock_guard<std::mutex> sl(sliceMutex_);
        for (auto it = sliceCache_.begin(); it != sliceCache_.end();) {
            if (frameNumber > it->lastFrame + 240) {
                if (it->tex.valid()) backend_->destroy_texture(it->tex);
                it = sliceCache_.erase(it);
            } else ++it;
        }
    }
    scene3d_.collect(frameNumber);
    // Aurea Particular: pontos/malhas de fontes e buffers de camadas que sumiram.
    particleStatics_.collect(frameNumber);
    particleExtraBufs_.collect(*backend_, frameNumber);
    for (auto it = planar_.begin(); it != planar_.end();) {
        if (frameNumber > it->second.lastFrame + 240) {
            for (TextureHandle& t : it->second.plane) if (t.valid()) backend_->destroy_texture(t);
            it = planar_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = flowCache_.begin(); it != flowCache_.end();) {
        if (frameNumber > it->second.lastFrame + 240) {
            for (TextureHandle& t : it->second.tex) {
                if (!t.valid()) continue;
                backend_->destroy_texture(t);
                heavyStats_.flowCacheBytes -= std::min<u64>(heavyStats_.flowCacheBytes, static_cast<u64>(it->second.width) * it->second.height * 8);
            }
            it = flowCache_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = maskCache_.begin(); it != maskCache_.end();) {
        if (frameNumber > it->second.lastFrame + 240) {
            for (TextureHandle& t : it->second.tex) if (t.valid()) backend_->destroy_texture(t);
            it = maskCache_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = luts_.begin(); it != luts_.end();) {
        if (frameNumber > it->second.lastFrame + 240) {
            backend_->destroy_texture(it->second.texture);
            it = luts_.erase(it);
        } else {
            ++it;
        }
    }
    // Espectros: cada quadro de som é uma textura nova; 60 quadros de folga
    // cobrem o vai-e-vem do scrubbing sem acumular.
    for (auto it = spectra_.begin(); it != spectra_.end();) {
        if (frameNumber > it->second.lastFrame + 60) {
            backend_->destroy_texture(it->second.texture);
            it = spectra_.erase(it);
        } else {
            ++it;
        }
    }
    // Mapas de profundidade: como os espectros, uma textura por quadro-fonte.
    (void)collect_depth(frameNumber, 60);
    // Versões lineares de imagens que saíram de cena (a RGBA8 original fica).
    for (auto& [k, img] : images_) {
        for (ImageTexture::Linear& L : img.linear) {
            if (L.tex.valid() && frameNumber > L.lastFrame + 240) {
                backend_->destroy_texture(L.tex);
                L = ImageTexture::Linear{};
            }
        }
    }
}

TextureHandle Renderer::scene_image_texture(u64 key, const ImagePixels* px, u64 frameNumber) noexcept {
    if (!backend_ || key == 0) return TextureHandle{};
    auto it = sceneImages_.find(key);
    if (it != sceneImages_.end() && px && (it->second.width != px->width || it->second.height != px->height)) {
        backend_->destroy_texture(it->second.texture);
        sceneImages_.erase(it);
        it = sceneImages_.end();
    }
    if (it != sceneImages_.end()) {
        it->second.lastFrame = frameNumber;
        return it->second.texture;
    }
    if (!px || !px->width || !px->height || px->rgba.size() < static_cast<usize>(px->width) * px->height * 4) return TextureHandle{};
    // O mesmo formato das texturas de cor dos modelos (GpuModel::upload).
    TextureDesc d;
    d.width = px->width;
    d.height = px->height;
    u32 levels = 1;
    for (u32 side = std::max(px->width, px->height); side > 1; side >>= 1) ++levels;
    d.mipLevels = levels;
    d.format = SurfaceFormat::RGBA8_sRGB;
    d.sampled = true;
    d.transferDst = true;
    d.debugName = "3d-textura-trocada";
    auto tex = backend_->create_texture(d);
    if (!tex.ok()) return TextureHandle{};
    PendingUpload up;
    up.texture = *tex;
    up.bytesPerRow = px->width * 4;
    up.data = px->rgba;
    up.mips = true;
    uploads_.push_back(std::move(up));
    sceneImages_[key] = ImageTexture{*tex, px->width, px->height, frameNumber};
    return *tex;
}

void Renderer::destroy_image_linear(ImageTexture& img) noexcept {
    for (ImageTexture::Linear& L : img.linear) {
        if (L.tex.valid() && backend_) backend_->destroy_texture(L.tex);
        L = ImageTexture::Linear{};
    }
}

u32 Renderer::trim_memory(u8 stage, u64 frameNumber) noexcept {
    if (stage >= 4) cancel_preview_graph();
    if (stage >= 4) { staticPoses_.clear(); staticPoseBytes_ = 0; poseWorkspace_ = {}; }
    if (!backend_ || stage < 4) return 0;
    u32 n = static_cast<u32>(previewFrames_.size());
    clear_preview_cache();
    n += backend_->trim_external_images();
    auto unused = [frameNumber](u64 lastFrame) { return lastFrame < frameNumber; };
    // Original image uploads and their linear copies are recreatable caches
    // too. Keeping every image visited on the timeline defeats OS memory trims.
    for (auto it = images_.begin(); it != images_.end();) {
        auto& img = it->second;
        const bool remove = unused(img.lastFrame);
        for (auto& linear : img.linear) {
            if (linear.tex.valid() && (remove || unused(linear.lastFrame))) {
                backend_->destroy_texture(linear.tex);
                linear = ImageTexture::Linear{};
                ++n;
            }
        }
        if (remove) {
            if (img.texture.valid()) { backend_->destroy_texture(img.texture); ++n; }
            it = images_.erase(it);
        } else ++it;
    }
    for (auto it = sceneImages_.begin(); it != sceneImages_.end();) {
        if (!unused(it->second.lastFrame)) { ++it; continue; }
        if (it->second.texture.valid()) { backend_->destroy_texture(it->second.texture); ++n; }
        it = sceneImages_.erase(it);
    }
    {
        // Pressão de memória: faixas do TimeSlice fora do quadro atual saem
        // (refeitas pelo decoder quando voltarem a ser pedidas).
        std::lock_guard<std::mutex> sl(sliceMutex_);
        for (auto it = sliceCache_.begin(); it != sliceCache_.end();) {
            if (!unused(it->lastFrame)) { ++it; continue; }
            if (it->tex.valid()) { backend_->destroy_texture(it->tex); ++n; }
            it = sliceCache_.erase(it);
        }
    }
    for (auto it = planar_.begin(); it != planar_.end();) {
        if (!unused(it->second.lastFrame)) { ++it; continue; }
        for (TextureHandle& t : it->second.plane) if (t.valid()) { backend_->destroy_texture(t); ++n; }
        it = planar_.erase(it);
    }
    for (auto it = flowCache_.begin(); it != flowCache_.end();) {
        if (!unused(it->second.lastFrame)) { ++it; continue; }
        for (TextureHandle& t : it->second.tex) if (t.valid()) { backend_->destroy_texture(t); ++n; }
        it = flowCache_.erase(it);
    }
    for (auto it = maskCache_.begin(); it != maskCache_.end();) {
        if (!unused(it->second.lastFrame)) { ++it; continue; }
        for (TextureHandle& t : it->second.tex) if (t.valid()) { backend_->destroy_texture(t); ++n; }
        it = maskCache_.erase(it);
    }
    for (auto it = luts_.begin(); it != luts_.end();) {
        if (!unused(it->second.lastFrame)) { ++it; continue; }
        backend_->destroy_texture(it->second.texture);
        ++n;
        it = luts_.erase(it);
    }
    for (auto it = spectra_.begin(); it != spectra_.end();) {
        if (!unused(it->second.lastFrame)) { ++it; continue; }
        backend_->destroy_texture(it->second.texture);
        ++n;
        it = spectra_.erase(it);
    }
    for (auto it = vectorCache_.begin(); it != vectorCache_.end();) {
        if (unused(it->second.lastFrame)) it = vectorCache_.erase(it);
        else ++it;
    }
    // Mapas de profundidade fora do quadro; a rede (~66 MB) e o decoder dela saem.
    n += collect_depth(frameNumber, 0);
    if (depth_) depth_->trim();
    if (foreground_) foreground_->trim();
    // Recortes do Roto Brush (até 48 MB de RLE na CPU): saem sob pressão
    // crítica (RUNNING_CRITICAL, MODERATE, COMPLETE). Refazem-se pelos traços.
    if (stage >= 6 && roto_) (void)roto_->trim_to(0);
    particleStatics_.collect(frameNumber, 0);
    particleExtraBufs_.collect(*backend_, frameNumber, 1);
    // Entre quadros nada do pool está em uso: tudo volta a nascer sob demanda.
    n += pool_.stats().alive;
    pool_.clear();
    if (stage >= 6) {
        // Environment timestamps use the backend frame clock, not prepare's
        // counter (which also advances while waiting for decoders/local AI).
        n += scene3d_.trim_environment_cache(renderFrameNumber_);
        scene3d_.collect(renderFrameNumber_, 0);
        n += scene3d_.trim_upload_pools();
    }
    return n;
}

void Renderer::read_timings(RenderTimings& t) noexcept {
    if (t.gpuProfileFragmented) {
        lastGpuPasses_ = 0;
        t.gpuMeasured = false;
        t.gpuTotalMs = t.gpuColorConvMs = t.gpuEffectsMs = t.gpuBlurMs = t.gpuGlowMs = t.gpuCompositeMs = t.gpuOutputMs = 0.0f;
        return;
    }
    f32 total = 0.0f;
    const u32 n = backend_->read_gpu_timings(timingScratch_.data(),
                                             static_cast<u32>(timingScratch_.size()), &total);
    lastGpuPasses_ = n;
    t.gpuMeasured = n > 0;
    if (!t.gpuMeasured) return;
    t.gpuTotalMs = total;
    t.gpuColorConvMs = t.gpuEffectsMs = t.gpuBlurMs = t.gpuGlowMs = t.gpuCompositeMs = t.gpuOutputMs = 0.0f;
    for (u32 i = 0; i < n; ++i) {
        const GpuTiming& g = timingScratch_[i];
        if (!g.label) continue;
        const char* s = g.label;
        auto starts = [s](const char* p) { return std::strncmp(s, p, std::strlen(p)) == 0; };
        if (starts("cor-do-video") || starts("imagem") || starts("solido")) t.gpuColorConvMs += g.ms;
        else if (starts("composicao")) t.gpuCompositeMs += g.ms;
        else if (starts("saida")) t.gpuOutputMs += g.ms;
        else {
            t.gpuEffectsMs += g.ms;
            if (starts("blur")) t.gpuBlurMs += g.ms;
            if (starts("glow")) t.gpuGlowMs += g.ms;
        }
    }
}

} // namespace aurea
