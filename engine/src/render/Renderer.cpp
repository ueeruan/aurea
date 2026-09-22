#include "aurea/render/Renderer.hpp"
#include "aurea/expr/Expression.hpp"

#include "aurea/scene3d/Animation.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/text/FontManager.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/project/Project.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea {
namespace {

constexpr SurfaceFormat kWorkFormat = SurfaceFormat::RGBA16F;

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

/// Valor da trilha num instante FRACIONÁRIO: interpola entre os dois quadros
/// vizinhos (sub-quadro do obturador). Exato para keyframe linear; nas curvas,
/// a corda de um quadro — invisível no desfoque.
/// `fallback` = valor parado da camada (sem keyframe); a expressão, se houver,
/// entra pelo `value_or`.
f32 sample_frac(const Track& tr, f64 t, f32 fallback) noexcept {
    const f64 f = std::floor(t);
    const f32 a = tr.value_or(FrameIndex{static_cast<i64>(f)}, fallback);
    const f32 k = static_cast<f32>(t - f);
    if (k <= 0.0f) return a;
    return a + (tr.value_or(FrameIndex{static_cast<i64>(f) + 1}, fallback) - a) * k;
}

/// `layer_matrix` num tempo local fracionário.
Mat4 layer_matrix_frac(const Layer& l, f64 local) noexcept {
    const TrackSet& t = l.tracks;
    auto s = [&](TrackProperty p, f32 fallback) noexcept {
        const Track* tr = t.find(p);
        return tr ? sample_frac(*tr, local, fallback) : fallback;
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

/// Transform 3D completo da layer (posição/rotação/escala/âncora em XYZ),
/// num tempo local fracionário (inteiro = o próprio quadro).
Mat4 layer_matrix_3d_frac(const Layer& l, f64 local) noexcept {
    const TrackSet& t = l.tracks;
    auto s = [&](TrackProperty p, f32 fallback) noexcept {
        const Track* tr = t.find(p);
        return tr ? sample_frac(*tr, local, fallback) : fallback;
    };
    const Vec3 pos{s(TrackProperty::PositionX, l.transform.position.x), s(TrackProperty::PositionY, l.transform.position.y),
                   s(TrackProperty::PositionZ, l.transform.position.z)};
    Vec3 scale{s(TrackProperty::ScaleX, l.transform.scale.x), s(TrackProperty::ScaleY, l.transform.scale.y),
               s(TrackProperty::ScaleZ, l.transform.scale.z)};
    // A profundidade acompanha a largura (Z multiplica X) em tudo que não é
    // câmera/luz: os controles de escala da UI mexem só em X/Y. Sem isto,
    // escalar um modelo — ou o NULO pai dele — o achataria em profundidade
    // (perfil e sombreamento esticam em vez de dar zoom).
    if (l.kind != LayerKind::Camera && l.kind != LayerKind::Light) scale.z *= scale.x;
    const Vec3 rot{s(TrackProperty::RotationX, l.transform.rotation.x) * kDeg2Rad,
                   s(TrackProperty::RotationY, l.transform.rotation.y) * kDeg2Rad,
                   s(TrackProperty::RotationZ, l.transform.rotation.z) * kDeg2Rad};
    const Vec3 anchor{s(TrackProperty::AnchorX, l.transform.anchor.x), s(TrackProperty::AnchorY, l.transform.anchor.y),
                      s(TrackProperty::AnchorZ, l.transform.anchor.z)};
    return Mat4::translation(pos) * Mat4::from_quat(Quat::from_euler_zyx(rot.x, rot.y, rot.z)) * Mat4::scale(scale)
         * Mat4::translation(-anchor);
}

Mat4 layer_matrix_3d(const Layer& l, FrameIndex local) noexcept {
    return layer_matrix_3d_frac(l, static_cast<f64>(local.value));
}

f32 layer_opacity(const Layer& l, FrameIndex local) noexcept {
    const Track* tr = l.tracks.find(TrackProperty::Opacity);
    const f32 v = tr ? tr->value_or(local, l.transform.opacity) : l.transform.opacity;
    return std::clamp(v, 0.0f, 1.0f);
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
bool wants_3d(const Composition& comp, const Layer& l, FrameIndex time) noexcept {
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
        p = p->parent.valid() ? comp.layer(p->parent) : nullptr;
    }
    return false;
}

/// Cadeia 2D (camada e pais) num instante fracionário da timeline.
Mat4 world_2d_frac(const Composition& comp, const Layer& l, f64 time) noexcept {
    auto localOf = [time](const Layer& x) {
        return time - static_cast<f64>(x.start.value) + static_cast<f64>(x.offset.value);
    };
    Mat4 m = layer_matrix_frac(l, localOf(l));
    LayerId parent = l.parent;
    for (u32 depth = 0; parent.valid() && depth < 16; ++depth) {
        const Layer* p = comp.layer(parent);
        if (!p) break;
        m = layer_matrix_frac(*p, localOf(*p)) * m;
        parent = p->parent;
    }
    return m;
}

/// Mundo 3D da camada com a cadeia de pais (cada pai no próprio tempo), num
/// instante fracionário da timeline (sub-quadro do obturador).
Mat4 world_3d_frac(const Composition& comp, const Layer& l, f64 time) noexcept {
    auto localOf = [time](const Layer& x) {
        return time - static_cast<f64>(x.start.value) + static_cast<f64>(x.offset.value);
    };
    Mat4 m = layer_matrix_3d_frac(l, localOf(l));
    LayerId parent = l.parent;
    for (u32 depth = 0; parent.valid() && depth < 16; ++depth) {
        const Layer* p = comp.layer(parent);
        if (!p) break;
        m = layer_matrix_3d_frac(*p, localOf(*p)) * m;
        parent = p->parent;
    }
    return m;
}

/// Mundo 3D da camada com a cadeia de pais (cada pai no próprio tempo).
Mat4 world_3d(const Composition& comp, const Layer& l, FrameIndex time) noexcept {
    return world_3d_frac(comp, l, static_cast<f64>(time.value));
}

/// Modelo 3D no instante (fracionário): mundo com a cadeia de pais e pose da
/// animação no relógio da TIMELINE (tempo local × velocidade do clipe).
void place_model(const Composition& comp, const Layer& l, const scene3d::SceneAsset& asset, f64 time,
                 scene3d::SceneInstance& inst) {
    inst.world = world_3d_frac(comp, l, time) * layer_from_model(l.model);
    scene3d::Pose pose;
    const i32 clip = l.model.animationClip;
    f32 t = 0.0f;
    if (clip >= 0 && clip < static_cast<i32>(asset.animations.size())) {
        const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
        const f64 local = time - static_cast<f64>(l.start.value) + static_cast<f64>(l.offset.value);
        t = scene3d::clip_time(asset.animations[static_cast<usize>(clip)], local / fps * l.model.timeScale);
    }
    scene3d::evaluate_pose(asset, clip, t, pose);
    inst.nodeWorld = std::move(pose.nodeWorld);
    inst.jointMatrices = std::move(pose.jointMatrices);
    inst.skinJointOffset = std::move(pose.skinJointOffset);
    inst.morphWeights = std::move(pose.morphWeights);
}

/// Câmera da composição no instante: a camada de câmera ATIVA visível; sem
/// ela, a padrão (plano Z=0 em escala 1:1 com a composição).
scene3d::SceneCamera camera_for_frac(const Composition& comp, f64 timeF, u32 w, u32 h) noexcept {
    const FrameIndex time{static_cast<i64>(std::floor(timeF))};
    scene3d::SceneCamera cam = scene3d::default_camera(w, h);
    const OrderedIds<LayerId>& order = comp.order();
    for (u32 i = 0; i < order.size(); ++i) {
        const Layer* l = comp.layer(order.at(i));
        if (!l || !l->visible || !l->contains_time(time)) continue;
        if (l->kind != LayerKind::Camera || !l->camera.active) continue;
        const f64 local = timeF - static_cast<f64>(l->start.value) + static_cast<f64>(l->offset.value);
        const Mat4 wm = world_3d_frac(comp, *l, timeF);
        const Vec3 x = Vec3{wm.col[0].x, wm.col[0].y, wm.col[0].z}.normalized();
        const Vec3 y = Vec3{wm.col[1].x, wm.col[1].y, wm.col[1].z}.normalized();
        const Vec3 z = Vec3{wm.col[2].x, wm.col[2].y, wm.col[2].z}.normalized();
        const Vec3 t{wm.col[3].x, wm.col[3].y, wm.col[3].z};
        Mat4 v;
        v.col[0] = Vec4{x.x, y.x, z.x, 0};
        v.col[1] = Vec4{x.y, y.y, z.y, 0};
        v.col[2] = Vec4{x.z, y.z, z.z, 0};
        v.col[3] = Vec4{-x.dot(t), -y.dot(t), -z.dot(t), 1};
        cam.view = v;
        cam.position = t;
        const Track* fov = l->tracks.find(TrackProperty::Fov);
        const f32 deg = fov ? sample_frac(*fov, local, l->camera.fov) : l->camera.fov;
        cam.fovY = std::clamp(deg, 1.0f, 170.0f) * kDeg2Rad;
        cam.nearZ = std::max(0.1f, l->camera.nearPlane);
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
         * scene3d::reverse_z_perspective(cam.fovY, static_cast<f32>(w) / static_cast<f32>(std::max(1u, h)), cam.nearZ) * cam.view;
}

} // namespace

Mat4 comp_view_projection(const Composition& comp, FrameIndex time) noexcept {
    const u32 w = std::max(1u, comp.width()), h = std::max(1u, comp.height());
    const scene3d::SceneCamera cam = camera_for(comp, time, w, h);
    return comp_from_clip(static_cast<f32>(w), static_cast<f32>(h))
         * scene3d::reverse_z_perspective(cam.fovY, static_cast<f32>(w) / static_cast<f32>(h), cam.nearZ) * cam.view;
}

Mat4 layer_world_3d(const Composition& comp, const Layer& l, FrameIndex time) noexcept { return world_3d(comp, l, time); }
bool wants_layer_3d(const Composition& comp, const Layer& l, FrameIndex time) noexcept { return wants_3d(comp, l, time); }

Mat4 layer_comp_matrix(const Composition& comp, const Layer& l, FrameIndex time, bool* perspective) noexcept {
    const bool is3d = wants_3d(comp, l, time);
    if (perspective) *perspective = is3d;
    if (!is3d) return layer_world_matrix(comp, l, time);
    const u32 w = std::max(1u, comp.width()), h = std::max(1u, comp.height());
    const scene3d::SceneCamera cam = camera_for(comp, time, w, h);
    return comp_from_clip(static_cast<f32>(w), static_cast<f32>(h))
         * scene3d::reverse_z_perspective(cam.fovY, static_cast<f32>(w) / static_cast<f32>(h), cam.nearZ) * cam.view
         * world_3d(comp, l, time);
}

Mat4 layer_world_matrix(const Composition& comp, const Layer& l, FrameIndex time) noexcept {
    if (wants_3d(comp, l, time)) return world_3d(comp, l, time);
    Mat4 m = layer_matrix(l, l.local_time(time));
    LayerId parent = l.parent;
    for (u32 depth = 0; parent.valid() && depth < 16; ++depth) {
        const Layer* p = comp.layer(parent);
        if (!p) break;
        m = layer_matrix(*p, p->local_time(time)) * m;
        parent = p->parent;
    }
    return m;
}

// =============================================================================
// Ciclo de vida
// =============================================================================
Renderer::Renderer() {
    draws_.reserve(64);
    framesInFlight_.reserve(16);
    timingScratch_.resize(128);
}

Renderer::~Renderer() { shutdown(); }

Status Renderer::initialize(GPUBackend& backend, const EffectRegistry& effects) noexcept {
    backend_ = &backend;
    effects_ = &effects;
    if (const Status s = shaders_.initialize(backend); !s.ok()) {
        backend_ = nullptr;
        return s;
    }

    // PRÉ-AQUECIMENTO: todo pipeline embutido é compilado aqui, antes do
    // primeiro frame. Com o cache de pipeline persistido pelo backend, da
    // segunda abertura em diante isto é quase instantâneo.
    if (const Status s = scene3d_.initialize(backend, shaders_); !s.ok()) {
        AUREA_LOG_ERROR("renderer: 3D indisponivel: %s", s.message().data());
    }
    std::vector<PipelineKey> keys;
    effects.collect_pipelines(keys, kWorkFormat);
    scene3d_.collect_pipelines(keys);
    keys.push_back(PipelineKey::fullscreen(ShaderId::video_yuv_planar_frag, kWorkFormat));
    keys.push_back(PipelineKey::fullscreen(ShaderId::video_rgba_to_linear_frag, kWorkFormat));
    keys.push_back(PipelineKey::fullscreen(ShaderId::shape_shape_frag, kWorkFormat));
    keys.push_back(PipelineKey::fullscreen(ShaderId::common_copy_frag, kWorkFormat));
    keys.push_back(PipelineKey::graphics(ShaderId::composite_layer_vert, ShaderId::composite_layer_frag,
                                         kWorkFormat, true, BlendMode::Normal));
    keys.push_back(PipelineKey::graphics(ShaderId::composite_layer_vert, ShaderId::composite_layer_frag,
                                         kWorkFormat, true, BlendMode::Add));
    for (SurfaceFormat f : {SurfaceFormat::RGBA8, SurfaceFormat::BGRA8}) {
        keys.push_back(PipelineKey::graphics(ShaderId::composite_layer_vert, ShaderId::composite_output_frag, f));
    }
    // Export: planos Y (R8) e CbCr (RG8).
    keys.push_back(PipelineKey::fullscreen(ShaderId::export_yuv_encode_frag, SurfaceFormat::R8));
    keys.push_back(PipelineKey::fullscreen(ShaderId::export_yuv_encode_frag, SurfaceFormat::RG8));
    prewarmed_ = shaders_.prewarm(keys.data(), static_cast<u32>(keys.size()));
    AUREA_LOG_INFO("renderer: %u pipelines pre-aquecidos", prewarmed_);
    return OkStatus;
}

void Renderer::shutdown() noexcept {
    if (!backend_) return;
    backend_->wait_idle();
    framesInFlight_.clear();
    release_project_resources();
    if (glyphAtlas_.valid()) backend_->destroy_texture(glyphAtlas_);
    glyphAtlas_ = TextureHandle{};
    for (u32 i = 0; i < kGlyphRing; ++i) {
        if (glyphBuf_[i].valid()) backend_->destroy_buffer(glyphBuf_[i]);
        glyphBuf_[i] = BufferHandle{};
        glyphCap_[i] = 0;
    }
    scene3d_.shutdown();
    pool_.clear();
    shaders_.shutdown();
    backend_ = nullptr;
}

void Renderer::forget_device() noexcept {
    glyphAtlas_ = TextureHandle{};
    glyphAtlasGen_ = 0;
    for (u32 i = 0; i < kGlyphRing; ++i) { glyphBuf_[i] = BufferHandle{}; glyphCap_[i] = 0; }
    framesInFlight_.clear();
    planar_.clear();
    flowCache_.clear();
    images_.clear();
    luts_.clear();
    uploads_.clear();
    scene3d_.forget_device();
    pool_.forget();
    shaders_.forget_device();
    backend_ = nullptr;
}

void Renderer::release_project_resources() noexcept {
    if (!backend_) return;
    for (auto& [k, p] : planar_) {
        for (TextureHandle& t : p.plane) if (t.valid()) backend_->destroy_texture(t);
    }
    for (auto& [k, i] : images_) backend_->destroy_texture(i.texture);
    for (auto& [k, l] : luts_) backend_->destroy_texture(l.texture);
    for (auto& [k, f] : flowCache_) for (TextureHandle& t : f.tex) if (t.valid()) backend_->destroy_texture(t);
    flowCache_.clear();
    planar_.clear();
    images_.clear();
    luts_.clear();
    uploads_.clear();
    scene3d_.release_all();
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

// =============================================================================
// Fase 1 — prepare (sob o lock do modelo)
// =============================================================================
void Renderer::prepare(const Composition& comp, const Project& project, FrameIndex time,
                       MediaManager* media, const ImagePixels* (*imageLookup)(void*, AssetId),
                       void* imageCtx, const RenderSettings& settings, u64 frameNumber,
                       i32 playDirection, DecodeMode decodeMode, f32 speed, FrameSnapshot& out) {
    frameNumber_ = frameNumber;
    // Expressões: a timeline do quadro (camada dona, outras camadas) e o memo
    // por (propriedade, quadro). O quadro é preparado sob o lock do modelo.
    const expr::Scope exprScope(project.timeline());
    out.layers.clear();
    out.nested.clear();
    out.glyphs.clear();
    out.target = FGTexture{};
    out.compWidth = comp.width();
    out.compHeight = comp.height();
    const Color bg = comp.background();
    out.background = Vec4{srgb_to_linear(bg.r), srgb_to_linear(bg.g), srgb_to_linear(bg.b),
                          comp.transparent_background() ? 0.0f : 1.0f};
    out.time = time;
    out.videoLayers = 0;
    out.staleVideoFrames = 0;
    out.missingVideoFrames = 0;

    const f32 previewFactor = static_cast<f32>(settings.previewNumerator)
                            / static_cast<f32>(std::max(1u, settings.previewDenominator));
    // Câmera da cena para camadas 2D que vivem no espaço 3D.
    const scene3d::SceneCamera cam3d = camera_for(comp, time, out.compWidth, out.compHeight);
    const Mat4 viewProj3d = scene3d::reverse_z_perspective(
                                cam3d.fovY, static_cast<f32>(out.compWidth) / static_cast<f32>(std::max(1u, out.compHeight)),
                                cam3d.nearZ) * cam3d.view;
    const Mat4 compFromClip = comp_from_clip(static_cast<f32>(out.compWidth), static_cast<f32>(out.compHeight));
    const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;

    // A ORDEM DO CORE É A ORDEM DE COMPOSIÇÃO: `order()` do fundo para a
    // frente. Nenhuma lista paralela, nenhum reordenamento por tipo.
    const OrderedIds<LayerId>& order = comp.order();
    const u32 n = order.size();
    u32 used = 0;
    out.scenes.clear();
    // Layers 3D CONSECUTIVAS na pilha formam um grupo (uma cena, uma
    // profundidade compartilhada: elas se ocluem). Qualquer layer 2D entre
    // elas fecha o grupo — a ordem da pilha continua sendo a lei.
    bool groupOpen = false;
    for (u32 i = 0; i < n; ++i) {
        const LayerId id = order.at(i);
        const Layer* l = comp.layer(id);
        if (!l || !l->visible || !l->contains_time(time)) continue;
        const FrameIndex local = l->local_time(time);

        // Dentro de uma pré-composição os ids se repetem (cada composição tem a
        // sua tabela): decoder, texto e caches precisam de uma chave própria.
        const LayerId rid = nestSalt_ == 0 ? id
                          : LayerId{0x40000000u | ((nestSalt_ & 0x3FFFu) << 16) | (id.index & 0xFFFFu), id.generation};
        RenderLayer rl;
        // Margem além da que a âncora do texto conta (animadores, fundo, sombra):
        // a fonte cresce em volta, o texto não anda.
        f32 srcShift = 0.0f;
        rl.id = rid;
        rl.blend = l->blendMode;
        rl.opacity = layer_opacity(*l, local);
        if (rl.opacity <= 0.0f) continue;   // invisível: nenhum passe, nenhum decode

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
                if (!px || !px->width || !px->height) continue;
                rl.source.kind = LayerSource::Kind::Image;
                rl.source.image = l->source;
                rl.source.pixels = px;
                rl.source.width = px->width;
                rl.source.height = px->height;
                break;
            }
            case LayerKind::Shape: {
                const ShapeData& sh = l->shape;
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
                rl.source.shapeParams = Vec4{sh.cornerRadius, sh.points, sh.innerRadius, fill ? 1.0f : 0.0f};
                rl.source.shapeFill = premul(sh.fillColor);
                rl.source.shapeStroke = premul(sh.strokeColor);
                rl.source.shapeStrokeWidth = stroke ? sh.strokeWidth : 0.0f;
                break;
            }
            case LayerKind::Model3D: {
                std::shared_ptr<const scene3d::SceneAsset> asset =
                    modelLookup_ ? modelLookup_(modelCtx_, l->model.scene) : nullptr;
                if (!asset) continue;   // asset ausente: a layer não desenha (a UI mostra "modelo ausente")
                scene3d::SceneInstance inst;
                // Matriz 3D para qualquer pai (num pai 2D comum ela é a mesma
                // da 2D): o mesmo mundo que world_3d e o parentesco usam.
                place_model(comp, *l, *asset, static_cast<f64>(time.value), inst);
                inst.layerKey = rid.pack();
                inst.motionBlur = l->motionBlur;
                inst.castShadows = l->model.castShadows;
                inst.assetKey = l->model.scene.pack();
                inst.asset = std::move(asset);
                if (groupOpen && !out.scenes.empty()) {
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
                groupOpen = true;
                break;
            }
            case LayerKind::Camera:
            case LayerKind::Light:
                continue;   // não desenham; entram na cena depois do laço
            case LayerKind::Text: {
                const auto font = text::FontManager::instance().font_for(l->text);
                if (!font || l->text.content.empty() || (l->text.color.w <= 0.0f && l->text.strokeWidth <= 0.0f)) continue;
                const TextData& T = l->text;
                const bool animated = text::has_animators(T);
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
                        if (const Track* tr = l->tracks.find(TrackProperty::TextAnimParam, ai, p)) {
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
                        if (a.props & kTextPropTracking) extra += maxAbs(ai, text::kTracking, a.tracking) * static_cast<f32>(std::min<usize>(T.content.size(), 200));
                    }
                    pad += std::min(extra, 4000.0f);
                }
                // Deslocamento de caractere troca as letras antes do layout.
                TextData shaped;
                const TextData* src = &T;
                if (animated) {
                    const std::string c = text::apply_char_offset(T, l->tracks, static_cast<f64>(local.value), fps);
                    if (c != T.content) { shaped = T; shaped.content = c; src = &shaped; }
                }
                text::TextLayout L;
                if (!text::layout_quads(*font, *src, pad, L)) continue;
                srcShift = pad - (T.strokeWidth > 0.0f ? T.strokeWidth + 2.0f : 2.0f);   // a mesma margem de recenter_text
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
                f64 open = 0.0;
                if (animated && l->motionBlur && comp.motion_blur().enabled && comp.motion_blur().shutterAngle > 0.0f) {
                    const MotionBlurSettings& mb = comp.motion_blur();
                    sets = std::clamp<u32>(settings.finalQuality ? mb.samples
                                                                 : static_cast<u32>(static_cast<f32>(mb.previewSamples) * std::clamp(settings.heavyScale, 0.1f, 1.0f)),
                                           2u, 16u);
                    open = std::clamp(static_cast<f64>(mb.shutterAngle), 0.0, 720.0) / 360.0;
                }
                std::vector<text::GlyphAnim> anim;
                for (u32 si = 0; si < sets; ++si) {
                    const f64 lt = static_cast<f64>(local.value) + (sets > 1 ? ((static_cast<f64>(si) + 0.5) / static_cast<f64>(sets) - 0.5) * open : 0.0);
                    text::evaluate_text_animators(T, l->tracks, lt, fps, units, L.chars, L.words, L.lines, anim);
                    auto glyphMatrix = [&](const text::GlyphQuad& q, const text::GlyphAnim& a) {
                        const Vec3 pivot{(q.x0 + q.x1) * 0.5f, (q.y0 + q.y1) * 0.5f, 0};
                        Mat4 m = Mat4::translation(pivot + a.translate + Vec3{a.trackingShift, 0, 0});
                        if (a.rotation.x != 0 || a.rotation.y != 0 || a.rotation.z != 0)
                            m = m * Mat4::from_quat(Quat::from_euler_zyx(a.rotation.x * kDeg2Rad, a.rotation.y * kDeg2Rad, a.rotation.z * kDeg2Rad));
                        if (a.skew != 0) {
                            Mat4 sk;
                            sk.col[1].x = -std::tan(std::clamp(a.skew, -80.0f, 80.0f) * kDeg2Rad);   // inclina para a direita
                            m = m * sk;
                        }
                        return m * Mat4::scale(Vec3{a.scale.x, a.scale.y, 1}) * Mat4::translation(-pivot);
                    };
                    const f32 w = 1.0f / static_cast<f32>(sets);
                    // Fundo (caixa arredondada atrás do texto): uv.x < 0 marca "sólido".
                    if (T.background && T.backgroundColor.w > 0.0f) {
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
                            gi.xform = Mat4::translation(Vec3{T.shadowOffset.x, T.shadowOffset.y, 0}) * glyphMatrix(q, a);
                            gi.misc = Vec4{0, 0, q.k, std::clamp(stroke + a.strokeAdd, 0.0f, std::max(0.0f, strokeMax))};
                            gi.extra = Vec4{std::max(0.0f, T.shadowBlur) * 0.5f + a.blur, 0, 0, 0};
                            out.glyphs.push_back(gi);
                        }
                    }
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
                        fill.w *= a.opacity * w;
                        sc.w *= a.opacity * w;
                        gi.fill = fill;
                        gi.stroke = sc;
                        gi.xform = glyphMatrix(q, a);
                        gi.misc = Vec4{0, 0, q.k, std::clamp(stroke + a.strokeAdd, 0.0f, std::max(0.0f, strokeMax))};
                        gi.extra = Vec4{a.blur, 0, 0, 0};
                        out.glyphs.push_back(gi);
                    }
                    if (si == 0) rl.source.glyphCount = static_cast<u32>(out.glyphs.size()) - rl.source.glyphFirst;
                }
                rl.source.glyphSets = sets;
                break;
            }
            case LayerKind::ParticleSystem: {
                // Partículas analíticas: o shader resolve tudo a partir do
                // tempo local — aqui só o bloco de parâmetros.
                const ParticleData& pd = l->particles;
                const f32 lw = static_cast<f32>(comp.width()), lh = static_cast<f32>(comp.height());
                const f32 tsec = static_cast<f32>(static_cast<f64>(local.value) / fps);
                const f32 rate = std::clamp(pd.rate, 0.1f, 1000000.0f);   // o shader analítico aceita milhões
                const f32 life = std::clamp(pd.lifetime, 0.05f, 60.0f);
                const u32 cap = settings.finalQuality ? std::max<u32>(1u, pd.maxParticles)
                                                      : std::max<u32>(1u, static_cast<u32>(static_cast<f32>(pd.maxParticles) * std::clamp(settings.heavyScale, 0.05f, 1.0f)));
                const u32 slots = std::min<u32>(cap,
                                                static_cast<u32>(std::ceil(rate * life * 1.25f)) + 1u);
                auto lin = [](Vec4 c) { return Vec4{srgb_to_linear(c.x), srgb_to_linear(c.y), srgb_to_linear(c.z), c.w}; };
                rl.source.kind = LayerSource::Kind::Particles;
                rl.source.width = comp.width();
                rl.source.height = comp.height();
                rl.source.particleBlock[0] = Vec4{rate, life, pd.speed, pd.spread * kDeg2Rad};
                // Gravidade do modelo: y para CIMA (−980 = cai); a tela tem y para baixo.
                rl.source.particleBlock[1] = Vec4{pd.gravity.x, -pd.gravity.y, pd.startSize, pd.endSize};
                rl.source.particleBlock[2] = Vec4{pd.startOpacity, pd.endOpacity, pd.direction * kDeg2Rad, static_cast<f32>(pd.seed % 1000003u)};
                rl.source.particleBlock[3] = Vec4{pd.emitterSize.x, pd.emitterSize.y, tsec, static_cast<f32>(slots)};
                rl.source.particleBlock[4] = lin(pd.startColor);
                rl.source.particleBlock[5] = lin(pd.endColor);
                rl.source.particleBlock[6] = Vec4{lw * 0.5f + pd.emitterOffset.x, lh * 0.5f + pd.emitterOffset.y, 0, 0};
                rl.source.particleSlots = slots;
                rl.source.particleAdditive = pd.blendMode == 1;
                break;
            }
            case LayerKind::Composition: {
                // Pré-composição: a filha no tempo da FONTE da camada (entrada,
                // velocidade, reverso), convertido para a taxa dela.
                const Composition* child = project.timeline().composition(l->nested.composition);
                if (!child || child == &comp || prepareDepth_ >= 8) continue;
                const f64 childFps = child->fps() > 0.0 ? child->fps() : fps;
                const i64 cf = static_cast<i64>(std::floor(l->source_frame(time) * childFps / fps + 1e-6));
                if (cf < 0 || cf >= child->duration().value) continue;
                auto snapChild = std::make_unique<FrameSnapshot>();
                const u32 savedSalt = nestSalt_;
                nestSalt_ = (savedSalt * 131u + l->nested.composition.index + 1u) & 0x3FFFu;
                if (nestSalt_ == 0) nestSalt_ = 1;
                ++prepareDepth_;
                prepare(*child, project, FrameIndex{cf}, media, imageLookup, imageCtx, settings, frameNumber,
                        playDirection, decodeMode, speed, *snapChild);
                --prepareDepth_;
                nestSalt_ = savedSalt;
                frameNumber_ = frameNumber;
                out.videoLayers += snapChild->videoLayers;
                out.staleVideoFrames += snapChild->staleVideoFrames;
                out.missingVideoFrames += snapChild->missingVideoFrames;
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
            // O grupo não tem efeitos de layer nesta fase (entram sobre o
            // resultado do grupo quando o 3D ganhar efeitos compatíveis).
            if (out.plans.size() <= used) out.plans.emplace_back();
            out.plans[used].clear();
            out.layers.push_back(std::move(rl));
            ++used;
            continue;
        }

        // Transform da layer, com a cadeia de pais (cada pai no próprio tempo).
        Mat4 m = layer_matrix(*l, local);
        LayerId parent = l->parent;
        for (u32 depth = 0; parent.valid() && depth < 16; ++depth) {
            const Layer* p = comp.layer(parent);
            if (!p) break;
            m = layer_matrix(*p, p->local_time(time)) * m;
            parent = p->parent;
        }
        const bool inScene3d = wants_3d(comp, *l, time);
        if (inScene3d) {
            // Rotação X/Y, profundidade, nulo 3D na cadeia: a MESMA câmera dos
            // modelos 3D (padrão = plano Z=0 1:1 com a composição).
            m = compFromClip * viewProj3d * world_3d(comp, *l, time);
        }
        const Mat4 shiftM = Mat4::translation(Vec3{-srcShift, -srcShift, 0});
        if (srcShift != 0.0f) m = m * shiftM;
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
        if (rl.source.kind == LayerSource::Kind::Text || rl.source.kind == LayerSource::Kind::Shape) {
            f32 k = 1.0f;
            const f32 want = std::min(onScreen, 4.0f) * previewFactor;
            while (k < want && k < 4.0f) k *= 2.0f;
            rl.texelScale = std::max(rl.texelScale, k);
        }

        // Desfoque de movimento (transform 2D, com pais): K amostras no
        // obturador centrado no quadro. Camada parada no intervalo = nada.
        if (l->motionBlur && comp.motion_blur().enabled) {
            const bool in3d = wants_3d(comp, *l, time);
            const MotionBlurSettings& mb = comp.motion_blur();
            const u32 k = std::clamp<u32>(settings.finalQuality ? mb.samples
                                                               : static_cast<u32>(static_cast<f32>(mb.previewSamples) * std::clamp(settings.heavyScale, 0.1f, 1.0f)),
                                          2u, 64u);
            const f64 open = std::clamp(static_cast<f64>(mb.shutterAngle), 0.0, 720.0) / 360.0;
            if (open > 0.0) {
                rl.blurMatrices.resize(k);
                bool moves = false;
                for (u32 i = 0; i < k; ++i) {
                    const f64 u = (static_cast<f64>(i) + 0.5) / static_cast<f64>(k) - 0.5;
                    const f64 ts = static_cast<f64>(time.value) + u * open;
                    rl.blurMatrices[i] = in3d ? comp_view_projection_frac(comp, ts, out.compWidth, out.compHeight) * world_3d_frac(comp, *l, ts)
                                              : world_2d_frac(comp, *l, ts);
                    if (srcShift != 0.0f) rl.blurMatrices[i] = rl.blurMatrices[i] * shiftM;
                    for (int c = 0; c < 4 && !moves; ++c) {
                        const Vec4 d = rl.blurMatrices[i].col[c] - rl.blurMatrices[0].col[c];
                        if (std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z) + std::fabs(d.w) > 1e-4f) moves = true;
                    }
                }
                if (!moves) rl.blurMatrices.clear();
            }
        }
        // Eco e RGB no tempo (transform 2D com pais, em instantes passados).
        if ((l->echoCount > 0 || l->rgbDelay > 0.0f) && !wants_3d(comp, *l, time)) {
            const f64 t0 = static_cast<f64>(time.value);
            rl.blurMatrices.clear();
            if (l->rgbDelay > 0.0f) {
                const f64 d = std::clamp(static_cast<f64>(l->rgbDelay), 0.0, 60.0);
                rl.temporal.push_back({world_2d_frac(comp, *l, t0), 1.0f, Vec3{1, 0, 0}});
                rl.temporal.push_back({world_2d_frac(comp, *l, t0 - d), 1.0f, Vec3{0, 1, 0}});
                rl.temporal.push_back({world_2d_frac(comp, *l, t0 - 2.0 * d), 1.0f, Vec3{0, 0, 1}});
            } else {
                rl.temporal.push_back({m, 1.0f, Vec3{0, 0, 0}});
            }
            if (srcShift != 0.0f && l->rgbDelay > 0.0f) for (auto& t : rl.temporal) t.m = t.m * shiftM;
            if (l->echoCount > 0) {
                const u32 n = std::min<u32>(l->echoCount, 16u);
                const f64 d = std::clamp(static_cast<f64>(l->echoDelay), 0.25, 120.0);
                f32 w = 1.0f;
                for (u32 i = 1; i <= n; ++i) {
                    w *= std::clamp(l->echoDecay, 0.0f, 1.0f);
                    if (w < 0.01f) break;
                    rl.temporal.push_back({world_2d_frac(comp, *l, t0 - static_cast<f64>(i) * d) * shiftM, w, Vec3{0, 0, 0}});
                }
            }
        }

        // Vídeo: pede o frame e pega o melhor disponível AGORA (nunca espera).
        if (rl.source.kind == LayerSource::Kind::Video && media) {
            const Asset* asset = project.asset(l->source);
            VideoSource* src = media->source_for(rid, l->source, *asset, frameNumber);
            if (src) {
                i64 mediaUs = static_cast<i64>(std::llround(l->source_frame(time) * 1e6 / fps));
                // Mistura de quadros: posição FRACIONÁRIA na grade da fonte.
                i64 nextUs = -1;
                f32 blendT = 0.0f;
                const bool wantVector = l->vectorBlur > 0.0f && comp.motion_blur().shutterAngle > 0.0f;
                if (l->frameBlend >= 1 || wantVector) {
                    if (const f64 srcFps = src->info().fps; srcFps > 0.0) {
                        const f64 pos = l->source_frame(time) / fps * srcFps;
                        const f64 idx = std::floor(pos + 1e-3);
                        const f64 frac = pos - idx;
                        const i64 us = static_cast<i64>(std::llround((idx + 1.0) * 1e6 / srcFps));
                        const bool inside = src->info().durationUs <= 0 || us < src->info().durationUs;
                        const bool between = l->frameBlend >= 1 && frac > 0.01 && frac < 0.99;
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
                if (const f64 srcFps = src->info().fps; srcFps > 0.0) {
                    const f64 idx = std::floor(static_cast<f64>(mediaUs) * srcFps / 1e6 + 1e-3);
                    mediaUs = static_cast<i64>(std::llround(idx * 1e6 / srcFps));
                }
                const i64 dur = src->info().durationUs;
                if (dur > 0) mediaUs = std::clamp<i64>(mediaUs, 0, dur - src->frame_duration_us() / 2);
                DecodeRequest req;
                // Com mistura: primeiro o quadro atual; com ele no cache, o
                // seguinte (os dois precisam estar lá ao mesmo tempo).
                bool haveA = false;
                if (nextUs >= 0) (void)src->frame_for(mediaUs, &haveA);
                req.targetUs = nextUs >= 0 && haveA ? nextUs : mediaUs;
                req.mode = decodeMode;
                // Clipe reverso ou congelado: o decoder não tem embalo para a frente.
                req.direction = (l->speed == 0.0f || l->timeRemapEnabled) ? 0 : (l->reversed ? -playDirection : playDirection);
                req.speed = speed * std::max(0.0f, l->speed);
                src->request(req);
                bool exact = false;
                rl.source.frame = src->frame_for(mediaUs, &exact);
                if (nextUs >= 0) {
                    bool exactB = false;
                    FrameRef b = src->frame_for(nextUs, &exactB);
                    if (exact && exactB) {
                        rl.source.frameB = std::move(b);
                        rl.source.blendT = blendT;
                        // Aparelho em estado crítico: o preview mistura em vez de
                        // calcular o movimento de pixels (o export mantém o modo).
                        rl.source.blendMode = (l->frameBlend == 2 && !settings.finalQuality && settings.heavyScale <= 0.25f) ? 1 : l->frameBlend;
                        // Desfoque vetorial: o fluxo cobre um quadro da FONTE;
                        // o obturador da composição dá a fração (× velocidade).
                        rl.source.vectorBlur = wantVector
                            ? std::clamp(l->vectorBlur, 0.0f, 2.0f) * comp.motion_blur().shutterAngle / 360.0f * std::max(0.0f, std::fabs(l->speed))
                            : 0.0f;
                    } else {
                        exact = false;   // falta um dos dois: o export espera; o preview mostra o que tem
                    }
                }
                rl.source.frameExact = exact;
                if (!rl.source.frame) ++out.missingVideoFrames;
                else if (!exact) ++out.staleVideoFrames;
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
            // Mesmo id com outro tamanho = outro conteúdo (projeto trocado
            // sem liberar, asset reimportado): a textura velha não serve.
            if (it != images_.end()
                && (it->second.width != rl.source.width || it->second.height != rl.source.height)) {
                backend_->destroy_texture(it->second.texture);
                images_.erase(it);
                it = images_.end();
            }
            if (it == images_.end()) {
                TextureDesc d;
                d.width = rl.source.width;
                d.height = rl.source.height;
                d.format = SurfaceFormat::RGBA8;
                d.sampled = true;
                d.transferDst = true;
                d.debugName = "imagem";
                auto tex = backend_->create_texture(d);
                if (tex.ok()) {
                    PendingUpload up;
                    up.texture = *tex;
                    up.bytesPerRow = rl.source.width * 4;
                    up.data = rl.source.pixels->rgba;
                    uploads_.push_back(std::move(up));
                    images_[key] = ImageTexture{*tex, rl.source.width, rl.source.height, frameNumber};
                }
            } else {
                it->second.lastFrame = frameNumber;
            }
            rl.source.pixels = nullptr;   // não vale fora do lock
        }

        if (out.plans.size() <= used) out.plans.emplace_back();
        LayerPlacement placement;
        placement.compFromLayer = m;
        placement.compWidth = out.compWidth;
        placement.compHeight = out.compHeight;
        placement.layerWidth = rl.source.width;
        placement.layerHeight = rl.source.height;
        EffectGraph::plan(*l, *effects_, local, rl.texelScale, placement, this, out.plans[used]);

        // Camada 2D no espaço 3D: entra na cena (profundidade de verdade com os
        // modelos e as outras camadas 3D vizinhas na pilha). Com desfoque/eco
        // ou modo de mistura ela continua na composição, como antes.
        const bool asPlane = inScene3d && rl.blurMatrices.empty() && rl.temporal.empty() && rl.blend == BlendMode::Normal
                          && rl.source.kind != LayerSource::Kind::Particles && !out.plans[used].hasFold;
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
            }
            rl.planeGroup = static_cast<i32>(out.scenes.size() - 1);
            out.scenes.back().planeLayers.push_back(used);
        } else {
            groupOpen = false;
        }
        out.layers.push_back(std::move(rl));
        ++used;
    }
    for (u32 k = used; k < out.plans.size(); ++k) out.plans[k].clear();
    if (!out.scenes.empty()) fill_scene_context(comp, time, out);
    // Desfoque de movimento 3D: cada grupo com modelo que pede desfoque vira
    // K cenas no obturador (câmera, mundo e pose no sub-quadro).
    if (!out.scenes.empty() && comp.motion_blur().enabled && comp.motion_blur().shutterAngle > 0.0f) {
        const MotionBlurSettings& mb = comp.motion_blur();
        const u32 k = std::clamp<u32>(settings.finalQuality ? mb.samples
                                                           : static_cast<u32>(static_cast<f32>(mb.previewSamples) * std::clamp(settings.heavyScale, 0.1f, 1.0f)),
                                      2u, 64u);
        const f64 open = std::clamp(static_cast<f64>(mb.shutterAngle), 0.0, 720.0) / 360.0;
        auto differs = [](const Mat4& a, const Mat4& b) {
            for (int c = 0; c < 4; ++c) {
                const Vec4 d = a.col[c] - b.col[c];
                if (std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z) + std::fabs(d.w) > 1e-4f) return true;
            }
            return false;
        };
        for (scene3d::SceneFrame& f : out.scenes) {
            f.blurFrames.clear();
            bool any = false;
            for (const scene3d::SceneInstance& in : f.instances) any |= in.motionBlur;
            if (!any) continue;
            bool moves = false;
            f.blurFrames.resize(k);
            for (u32 s = 0; s < k; ++s) {
                const f64 ts = static_cast<f64>(time.value) + ((static_cast<f64>(s) + 0.5) / static_cast<f64>(k) - 0.5) * open;
                scene3d::SceneFrame& sf = f.blurFrames[s];
                sf.camera = camera_for_frac(comp, ts, out.compWidth, out.compHeight);
                sf.lights = f.lights;
                sf.environment = f.environment;
                sf.instances = f.instances;
                for (scene3d::SceneInstance& in : sf.instances) {
                    if (!in.motionBlur) continue;
                    const Layer* l = comp.layer(LayerId::unpack(in.layerKey));
                    if (l && in.asset) place_model(comp, *l, *in.asset, ts, in);
                }
                if (s > 0 && !moves) {
                    const scene3d::SceneFrame& s0 = f.blurFrames[0];
                    moves = differs(sf.camera.view, s0.camera.view);
                    for (usize i = 0; i < sf.instances.size() && !moves; ++i) {
                        moves = differs(sf.instances[i].world, s0.instances[i].world);
                        const auto& a = sf.instances[i].nodeWorld;
                        const auto& b = s0.instances[i].nodeWorld;
                        for (usize n = 0; n < a.size() && n < b.size() && !moves; ++n) moves = differs(a[n], b[n]);
                    }
                }
            }
            if (!moves) f.blurFrames.clear();   // nada se mexe no obturador: uma cena só
        }
    }
}

void Renderer::fill_scene_context(const Composition& comp, FrameIndex time, FrameSnapshot& out) const noexcept {
    // Câmera: a camada de câmera ATIVA visível no instante (com pais, inclusive
    // nulo 3D — rig de órbita); sem ela, a padrão.
    const scene3d::SceneCamera cam = camera_for(comp, time, out.compWidth, out.compHeight);
    std::vector<scene3d::SceneLight> lights;
    const OrderedIds<LayerId>& order = comp.order();
    for (u32 i = 0; i < order.size(); ++i) {
        const Layer* l = comp.layer(order.at(i));
        if (!l || !l->visible || !l->contains_time(time)) continue;
        const FrameIndex local = l->local_time(time);
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
            s.color = l->light.color.xyz();
            s.intensity = l->light.intensity;
            s.range = l->light.range;
            s.outerCone = l->light.coneAngle * 0.5f * kDeg2Rad;
            s.innerCone = s.outerCone * (1.0f - std::clamp(l->light.penumbra, 0.0f, 1.0f));
            s.castShadows = l->light.castShadows;
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
    }
    for (scene3d::SceneFrame& f : out.scenes) {
        f.camera = cam;
        f.lights = lights;
        f.environment.intensity = env.intensity;
        f.environment.rotation = env.rotation;
        f.environment.hdri = env.hdri;
        f.environment.hdriKey = env.hdriKey;
    }
}

// =============================================================================
// Fase 2 — render (sem lock)
// =============================================================================
void Renderer::upload_glyphs(FrameSnapshot& snap) noexcept {
    // Todos os snapshots (pré-composições incluídas) num buffer só do quadro.
    usize total = 0;
    std::vector<FrameSnapshot*> all;
    std::vector<FrameSnapshot*> stack{&snap};
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
    if (!glyphBuf_[slot].valid() || !backend_->map_buffer(glyphBuf_[slot], ptr).ok() || !ptr) return;
    auto* dst = static_cast<GlyphInstance*>(ptr);
    for (FrameSnapshot* s : all) std::copy(s->glyphs.begin(), s->glyphs.end(), dst + s->glyphBase);
    backend_->unmap_buffer(glyphBuf_[slot]);
    glyphFrameBuf_ = glyphBuf_[slot];
}

void Renderer::flush_uploads() noexcept {
    for (PendingUpload& up : uploads_) {
        if (const Status s = backend_->upload_texture(up.texture, up.data.data(), up.bytesPerRow); !s.ok()) {
            AUREA_LOG_WARN("upload de textura falhou: %s", s.message().data());
        }
    }
    uploads_.clear();
}

bool Renderer::build_video_source(const RenderLayer& layer, u32 layerIndex, u32 w, u32 h,
                                  FGTexture target, u64 frameNumber) noexcept {
    return build_video_source(layer, layerIndex, w, h, target, frameNumber, layer.source.frame.get());
}

bool Renderer::build_video_source(const RenderLayer& layer, u32 layerIndex, u32 w, u32 h,
                                  FGTexture target, u64 frameNumber, DecodedFrame* f) noexcept {
    if (!f) return false;
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
            if (!pipe.ok()) return false;

            TextureDesc d;
            d.width = f->width;
            d.height = f->height;
            d.format = SurfaceFormat::RGBA8;
            const FGTexture extTex = graph_.import_texture("frame-do-decoder", imported->texture, d);
            void* ubo = arena_.alloc(sizeof(u), 16);
            std::memcpy(ubo, &u, sizeof(u));
            struct Cap { PipelineHandle p; FGTexture t; u64 sampler; void* ubo; } cap{*pipe, extTex, imported->sampler.id, ubo};
            const u32 pass = graph_.add_raster_pass("cor-do-video", PassStage::Decode, target, LoadOp::DontCare,
                                                    Vec4{0, 0, 0, 0}, [cap](PassContext& pc) {
                pc.cmds.bind_pipeline(cap.p);
                pc.cmds.bind_texture(0, pc.texture(cap.t), SamplerHandle{cap.sampler});
                pc.cmds.set_uniforms(cap.ubo, sizeof(YuvUniforms));
                pc.cmds.draw(3);
            });
            graph_.read(pass, extTex);
            lastZeroCopy_ = true;
            return true;
        }
        AUREA_LOG_WARN("importacao zero-copy falhou; sem planos de CPU para cair no fallback");
        return false;
    }

    // FALLBACK: planos na CPU (aparelho sem importação de AHardwareBuffer, ou
    // host de testes). Uma textura por plano, persistente por layer, sobe a
    // cada frame novo.
    if (f->planeCount < 2 || !f->planes[0]) return false;
    const bool tenBit = f->format == PixelFormat::P010;
    const bool threePlane = f->format == PixelFormat::YUV420P;
    // O quadro seguinte da mistura sobe em texturas próprias (senão o upload
    // dele sobrescreveria o do atual antes do passe de cor).
    const u64 key = layer.id.pack() ^ (f == layer.source.frameB.get() ? 0x8000000000000000ull : 0ull);
    PlanarTextures& pt = planar_[key];
    if (pt.width != f->width || pt.height != f->height || pt.format != f->format) {
        for (TextureHandle& t : pt.plane) {
            if (t.valid()) backend_->destroy_texture(t);
            t = TextureHandle{};
        }
        pt.width = f->width;
        pt.height = f->height;
        pt.format = f->format;
        const u32 planes = threePlane ? 3u : 2u;
        for (u32 p = 0; p < planes; ++p) {
            TextureDesc d;
            d.width = p == 0 ? f->width : (f->width + 1) / 2;
            d.height = p == 0 ? f->height : (f->height + 1) / 2;
            d.format = p == 0 || threePlane ? (tenBit ? SurfaceFormat::R16 : SurfaceFormat::R8)
                                            : (tenBit ? SurfaceFormat::RG16 : SurfaceFormat::RG8);
            d.sampled = true;
            d.transferDst = true;
            d.debugName = "plano-de-video";
            auto tex = backend_->create_texture(d);
            if (!tex.ok()) return false;
            pt.plane[p] = *tex;
        }
    }
    pt.lastFrame = frameNumber;
    const u32 planes = threePlane ? 3u : 2u;
    for (u32 p = 0; p < planes; ++p) {
        if (!f->planes[p]) return false;
        if (!backend_->upload_texture(pt.plane[p], f->planes[p], f->strides[p]).ok()) return false;
    }

    const f32 layoutKind = f->format == PixelFormat::NV21 ? 1.0f : (threePlane ? 2.0f : 0.0f);
    // P010 amostrado como unorm16: código de 10 bits = amostra * 65535 / (64 * 1023).
    const f32 codeScale = tenBit ? 65535.0f / (64.0f * 1023.0f) : 1.0f;
    u.sampling = Vec4{layoutKind, codeScale, taps ? 1.0f : 0.0f, 0.0f};

    EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat, 4096);
    const SamplerHandle lin = shaders_.sampler(CommonSampler::LinearClamp);
    (void)lin;
    return ctx.fullscreen_pass("cor-do-video", PassStage::Decode, target, ShaderId::video_yuv_planar_frag,
                               {PassTexture{{}, pt.plane[0], CommonSampler::LinearClamp},
                                PassTexture{{}, pt.plane[1], CommonSampler::LinearClamp},
                                PassTexture{{}, threePlane ? pt.plane[2] : pt.plane[1], CommonSampler::LinearClamp}},
                               &u, sizeof(u)) != kInvalidIndex;
}

FGTexture Renderer::video_flow(u64 layerKey, u64 pairKey, FGTexture a, FGTexture b, u32 w, u32 h, u32& baseW, u32& baseH,
                               u64 frameNumber) noexcept {
    // Base da pirâmide com o lado maior ≤ 384 (o flow não precisa de 4K).
    const f32 base = std::max(96.0f, 384.0f * std::clamp(heavyScale_, 0.25f, 1.0f));
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
        for (TextureHandle& t : fc.tex) if (t.valid()) backend_->destroy_texture(t);
        fc = FlowCache{};
        fc.width = lw;
        fc.height = lh;
        fc.lastFrame = frameNumber;
    }
    for (u32 i = 0; i < 2 && flowCacheEnabled_; ++i) {
        if (fc.tex[i].valid() && fc.key[i] == pairKey) {
            ++flowHits_;
            return graph_.import_texture("flow-cache", fc.tex[i], fd);
        }
    }
    ++flowMisses_;
    const u32 slot = fc.next;
    fc.next ^= 1u;
    if (!fc.tex[slot].valid()) {
        auto t = backend_->create_texture(fd);
        if (!t.ok()) return FGTexture{};
        fc.tex[slot] = *t;
    }
    fc.key[slot] = pairKey;
    EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat, 4096);
    FGTexture levels[6];
    u32 sizes[6][2];
    levels[0] = ctx.texture("flow-nivel", lw, lh);
    sizes[0][0] = lw;
    sizes[0][1] = lh;
    const Vec4 lumaParams{1.0f / static_cast<f32>(lw), 1.0f / static_cast<f32>(lh), 0, 0};
    ctx.fullscreen_pass("flow-luma", PassStage::Decode, levels[0], ShaderId::video_flow_luma_frag,
                        {PassTexture{a}, PassTexture{b}}, &lumaParams, sizeof(lumaParams));
    u32 n = 1;
    while (n < 6 && std::min(lw, lh) >= 12) {
        const u32 nw = (lw + 1) / 2, nh = (lh + 1) / 2;
        struct { Vec4 uvMap; Vec4 texel; } dp{Vec4{1, 1, 0, 0}, Vec4{1.0f / static_cast<f32>(lw), 1.0f / static_cast<f32>(lh), 0, 0}};
        levels[n] = ctx.texture("flow-nivel", nw, nh);
        ctx.fullscreen_pass("flow-reduz", PassStage::Decode, levels[n], ShaderId::effects_downsample_frag,
                            {PassTexture{levels[n - 1]}}, &dp, sizeof(dp));
        sizes[n][0] = lw = nw;
        sizes[n][1] = lh = nh;
        ++n;
    }
    FGTexture flow{};
    bool haveFlow = false;
    for (u32 i = n; i-- > 0;) {
        const f32 fw = static_cast<f32>(sizes[i][0]), fh = static_cast<f32>(sizes[i][1]);
        struct { Vec4 texel; Vec4 flags; } lp{Vec4{1.0f / fw, 1.0f / fh, fw, fh}, Vec4{haveFlow ? 1.0f : 0.0f, 6.0f, 0, 0}};
        // O nível base vai direto para a textura do cache.
        const FGTexture f = i == 0 ? graph_.import_texture("flow-cache", fc.tex[slot], fd) : ctx.texture("flow", sizes[i][0], sizes[i][1]);
        ctx.fullscreen_pass("flow-lk", PassStage::Decode, f, ShaderId::video_flow_lk_frag,
                            {PassTexture{levels[i]}, PassTexture{haveFlow ? flow : levels[i]}}, &lp, sizeof(lp));
        flow = f;
        haveFlow = true;
    }
    return flow;
}

bool Renderer::build_source(const RenderLayer& layer, u32 layerIndex, bool hasEffects, LayerImage& out,
                            std::vector<FrameRef>& framesUsed, u64 frameNumber) noexcept {
    if (layer.source.kind == LayerSource::Kind::Nested) {
        if (!currentSnap_ || layer.source.nestedIndex >= currentSnap_->nested.size()) return false;
        const FrameSnapshot* child = currentSnap_->nested[layer.source.nestedIndex].get();
        if (!child || !child->target.valid()) return false;
        out.texture = child->target;
        out.region = Rect{0.0f, 0.0f, static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height)};
        out.width = std::max(1u, static_cast<u32>(std::lround(static_cast<f32>(layer.source.width) * layer.texelScale)));
        out.height = std::max(1u, static_cast<u32>(std::lround(static_cast<f32>(layer.source.height) * layer.texelScale)));
        return true;
    }
    if (layer.source.kind == LayerSource::Kind::Scene3D) {
        // O grupo 3D renderiza no tamanho do alvo da composição (resolução de
        // preview incluída) e cobre a composição inteira.
        if (!currentScenes_ || layer.source.sceneGroup >= currentScenes_->size()) return false;
        const scene3d::SceneFrame& group = (*currentScenes_)[layer.source.sceneGroup];
        FGTexture tex{};
        const std::vector<scene3d::ScenePlane>* planes =
            layer.source.sceneGroup < groupPlanes_.size() && !groupPlanes_[layer.source.sceneGroup].empty() ? &groupPlanes_[layer.source.sceneGroup] : nullptr;
        if (group.blurFrames.empty()) {
            if (!scene3d_.build(graph_, arena_, group, compTargetW_, compTargetH_, frameNumber, tex, planes)) return false;
        } else {
            // Desfoque: K cenas no obturador, média aditiva (peso 1/K, cor
            // pré-multiplicada) num alvo do tamanho da composição.
            const u32 k = static_cast<u32>(group.blurFrames.size());
            FGTexture* frames = arena_.alloc_array<FGTexture>(k);
            u32 built = 0;
            for (u32 s = 0; s < k; ++s) {
                if (scene3d_.build(graph_, arena_, group.blurFrames[s], compTargetW_, compTargetH_, frameNumber, frames[built], planes)) ++built;
            }
            auto pAdd = shaders_.pipeline(PipelineKey::graphics(
                ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Add));
            if (built == 0 || !pAdd.ok()) return false;
            TextureDesc ad;
            ad.width = compTargetW_;
            ad.height = compTargetH_;
            ad.format = kWorkFormat;
            ad.sampled = true;
            ad.renderTarget = true;
            tex = graph_.create_texture("3d-desfoque", ad);
            struct Cap {
                FGTexture* frames; u32 n; PipelineHandle p; u64 sampler; f32 w; f32 h;
            } cap{frames, built, *pAdd, shaders_.sampler(CommonSampler::LinearClamp).id,
                  static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height)};
            const u32 pass = graph_.add_raster_pass("3d-desfoque", PassStage::Composite, tex, LoadOp::Clear, Vec4{0, 0, 0, 0},
                                                    [cap](PassContext& pc) {
                const Mat4 clip = clip_from_comp(cap.w, cap.h);
                pc.cmds.bind_pipeline(cap.p);
                for (u32 s = 0; s < cap.n; ++s) {
                    pc.cmds.bind_texture(0, pc.texture(cap.frames[s]), SamplerHandle{cap.sampler});
                    LayerPush push;
                    push.clipFromLayer = clip;
                    push.region = Vec4{0.0f, 0.0f, cap.w, cap.h};
                    push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
                    push.params = Vec4{1.0f / static_cast<f32>(cap.n), 0, 0, 0};
                    pc.cmds.push_constants(&push, sizeof(push));
                    pc.cmds.draw(6);
                }
            });
            for (u32 s = 0; s < built; ++s) graph_.read(pass, frames[s]);
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
            if (!build_video_source(layer, layerIndex, w, h, out.texture, frameNumber)) return false;
            framesUsed.push_back(layer.source.frame);
            // Quadro seguinte da fonte: mistura, movimento de pixels e/ou
            // desfoque vetorial (os dois últimos pelo optical flow, em cache).
            if (layer.source.frameB) {
                const FGTexture b = graph_.create_texture("layer-video-seguinte", d);
                if (!build_video_source(layer, layerIndex, w, h, b, frameNumber, layer.source.frameB.get())) return true;
                framesUsed.push_back(layer.source.frameB);
                EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat, maxTex);
                const bool needFlow = (layer.source.blendMode == 2 && layer.source.blendT > 0.0f) || layer.source.vectorBlur > 0.0f;
                FGTexture flow{};
                u32 baseW = 0, baseH = 0;
                if (needFlow) {
                    const u64 pair = (static_cast<u64>(layer.source.frame->ptsUs) * 1000003ull) ^ static_cast<u64>(layer.source.frameB->ptsUs)
                                   ^ (static_cast<u64>(w) << 40) ^ (static_cast<u64>(h) << 52);
                    flow = video_flow(layer.id.pack(), pair, out.texture, b, w, h, baseW, baseH, frameNumber);
                }
                if (layer.source.blendT > 0.0f && layer.source.blendMode == 2) {
                    const FGTexture moved = graph_.create_texture("layer-video-movimento", d);
                    const Vec4 wp{layer.source.blendT, 1.0f / static_cast<f32>(baseW), 1.0f / static_cast<f32>(baseH), 0};
                    ctx.fullscreen_pass("flow-deforma", PassStage::Decode, moved, ShaderId::video_flow_warp_frag,
                                        {PassTexture{out.texture}, PassTexture{b}, PassTexture{flow}}, &wp, sizeof(wp));
                    out.texture = moved;
                } else if (layer.source.blendT > 0.0f && layer.source.blendMode == 1) {
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
                if (layer.source.vectorBlur > 0.0f) {
                    // Borrão ao longo do vetor de cada pixel, obturador centrado no quadro.
                    const FGTexture blurred = graph_.create_texture("layer-video-desfoque-vetorial", d);
                    const Vec4 vp{layer.source.vectorBlur, 1.0f / static_cast<f32>(baseW), 1.0f / static_cast<f32>(baseH), 16.0f};
                    ctx.fullscreen_pass("desfoque-vetorial", PassStage::Decode, blurred, ShaderId::video_flow_vblur_frag,
                                        {PassTexture{out.texture}, PassTexture{flow}}, &vp, sizeof(vp));
                    out.texture = blurred;
                }
            }
            return true;
        }
        case LayerSource::Kind::Text: {
            // Glifos instanciados no atlas SDF, na densidade da layer na tela.
            if (!glyphFrameBuf_.valid() || !glyphAtlas_.valid() || layer.source.glyphCount == 0 || !currentSnap_) return false;
            auto pipe = shaders_.pipeline(PipelineKey::graphics(ShaderId::text_glyph_vert, ShaderId::text_glyph_frag, kWorkFormat, true,
                                                                BlendMode::Normal));
            if (!pipe.ok()) return false;
            const u32 sets = std::max(1u, layer.source.glyphSets);
            struct Cap { PipelineHandle p; TextureHandle atlas; u64 sampler; BufferHandle buf; Mat4 clip; Vec4 params; u32 count; };
            const Mat4 clip = clip_from_comp(static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height));
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
                drawSet(out.texture, 0);
                return true;
            }
            // Desfoque de movimento por letra: cada instante num alvo (letras
            // por cima umas das outras certinho) e a média (os glifos já têm 1/K).
            auto pAdd = shaders_.pipeline(PipelineKey::graphics(
                ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Add));
            if (!pAdd.ok()) return false;
            FGTexture* frames = arena_.alloc_array<FGTexture>(sets);
            for (u32 si = 0; si < sets; ++si) {
                frames[si] = graph_.create_texture("texto-instante", d);
                drawSet(frames[si], si);
            }
            out.texture = graph_.create_texture("layer-texto", d);
            struct Acc { FGTexture* frames; u32 n; PipelineHandle p; u64 sampler; f32 w; f32 h; } acc{frames, sets, *pAdd,
                shaders_.sampler(CommonSampler::LinearClamp).id, static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height)};
            const u32 pass = graph_.add_raster_pass("texto-desfoque", PassStage::Decode, out.texture, LoadOp::Clear, Vec4{0, 0, 0, 0},
                                                    [acc](PassContext& pc) {
                pc.cmds.bind_pipeline(acc.p);
                for (u32 si = 0; si < acc.n; ++si) {
                    pc.cmds.bind_texture(0, pc.texture(acc.frames[si]), SamplerHandle{acc.sampler});
                    LayerPush push;
                    push.clipFromLayer = clip_from_comp(acc.w, acc.h);
                    push.region = Vec4{0.0f, 0.0f, acc.w, acc.h};
                    push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
                    push.params = Vec4{1.0f, 0, 0, 0};
                    pc.cmds.push_constants(&push, sizeof(push));
                    pc.cmds.draw(6);
                }
            });
            for (u32 si = 0; si < sets; ++si) graph_.read(pass, frames[si]);
            return true;
        }
        case LayerSource::Kind::Image: {
            auto it = images_.find(layer.source.image.pack());
            if (it == images_.end()) return false;
            out.texture = graph_.create_texture("layer-imagem", d);
            EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat, maxTex);
            const Vec4 flags{0.0f, 0.0f, 0.0f, 0.0f};
            return ctx.fullscreen_pass("imagem", PassStage::Decode, out.texture, ShaderId::video_rgba_to_linear_frag,
                                       {PassTexture{{}, it->second.texture, CommonSampler::LinearClamp}},
                                       &flags, sizeof(flags)) != kInvalidIndex;
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
            struct ShapeBlock {
                Vec4 size, shape, fill, stroke, extra;
            } block{};
            const f32 lw = static_cast<f32>(layer.source.width), lh = static_cast<f32>(layer.source.height);
            block.size = Vec4{lw, lh, lw / static_cast<f32>(w), static_cast<f32>(layer.source.shapeType)};
            block.shape = layer.source.shapeParams;
            block.fill = layer.source.shapeFill;
            block.stroke = layer.source.shapeStroke;
            block.extra = Vec4{layer.source.shapeStrokeWidth, 0, 0, 0};
            return ctx.fullscreen_pass("forma", PassStage::Decode, out.texture, ShaderId::shape_shape_frag, {},
                                       &block, sizeof(block)) != kInvalidIndex;
        }
        case LayerSource::Kind::Particles: {
            out.texture = graph_.create_texture("layer-particulas", d);
            auto pipe = shaders_.pipeline(PipelineKey::graphics(
                ShaderId::particles_particles_vert, ShaderId::particles_particles_frag, kWorkFormat, true,
                layer.source.particleAdditive ? BlendMode::Add : BlendMode::Normal));
            if (!pipe.ok()) return false;
            struct Cap {
                PipelineHandle p; Mat4 clip; Vec4 block[7]; u32 slots;
            } cap{*pipe, clip_from_comp(static_cast<f32>(layer.source.width), static_cast<f32>(layer.source.height)), {},
                  layer.source.particleSlots};
            for (int i = 0; i < 7; ++i) cap.block[i] = layer.source.particleBlock[i];
            graph_.add_raster_pass("particulas", PassStage::Decode, out.texture, LoadOp::Clear, Vec4{0, 0, 0, 0},
                                   [cap](PassContext& pc) {
                pc.cmds.bind_pipeline(cap.p);
                pc.cmds.set_uniforms(cap.block, sizeof(cap.block));
                pc.cmds.push_constants(&cap.clip, sizeof(cap.clip));
                pc.cmds.draw(6, cap.slots);
            });
            return true;
        }
        case LayerSource::Kind::None: break;
    }
    return false;
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

    // --- Layers: fonte → efeitos → desenho na composição.
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
        if (!build_source(layer, i, hasEffects, src, framesInFlight_, frameNumber)) {
            if (layer.source.kind != LayerSource::Kind::Video) incomplete_ = true;
            continue;
        }
        LayerImage fin = src;
        if (hasEffects) (void)EffectGraph::build(snap.plans[i], ctx, src, fin);
        scene3d::ScenePlane p;
        p.texture = fin.texture;
        // Sólido otimizado (1×1): clamp, como na composição (a borda transparente
        // viraria um degradê).
        p.sampler = shaders_.sampler(layer.source.kind == LayerSource::Kind::Solid && fin.width == 1 ? CommonSampler::LinearClamp
                                                                                                      : CommonSampler::LinearBorder).id;
        p.clipFromLayer = clip_from_comp(static_cast<f32>(snap.compWidth), static_cast<f32>(snap.compHeight)) * layer.compFromLayer;
        p.region = Vec4{fin.region.x, fin.region.y, fin.region.w, fin.region.h};
        p.opacity = layer.opacity;
        const Vec4 c = p.clipFromLayer * Vec4{fin.region.x + fin.region.w * 0.5f, fin.region.y + fin.region.h * 0.5f, 0, 1};
        p.viewDepth = c.w;
        groupPlanes_[static_cast<usize>(layer.planeGroup)].push_back(p);
    }
    for (u32 i = 0; i < snap.layers.size(); ++i) {
        const RenderLayer& layer = snap.layers[i];
        if (layer.planeGroup >= 0) continue;   // desenhada dentro da cena
        LayerImage src;
        const bool hasEffects = i < snap.plans.size() && !snap.plans[i].empty();
        if (!build_source(layer, i, hasEffects, src, framesInFlight_, frameNumber)) {
            // Camada fora deste quadro por recurso ainda não pronto (pipeline
            // compilando, textura a caminho): o próximo quadro tenta de novo.
            if (layer.source.kind != LayerSource::Kind::Video) incomplete_ = true;
            continue;
        }
        LayerImage fin = src;
        if (i < snap.plans.size() && !snap.plans[i].empty()) {
            (void)EffectGraph::build(snap.plans[i], ctx, src, fin);
        }
        CompositeDraw draw;
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
        if (!layer.blurMatrices.empty() || !layer.temporal.empty()) {
            // Acumula as amostras no tempo (pré-multiplicadas, com peso, soma
            // aditiva) num alvo do tamanho da composição: desfoque = média
            // (peso 1/K); eco = soma com queda; RGB = um canal por amostra.
            auto pAdd = shaders_.pipeline(PipelineKey::graphics(
                ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Add));
            if (pAdd.ok()) {
                const bool temporal = !layer.temporal.empty();
                const u32 k = static_cast<u32>(temporal ? layer.temporal.size() : layer.blurMatrices.size());
                Mat4* mats = arena_.alloc_array<Mat4>(k);
                Vec4* prm = arena_.alloc_array<Vec4>(k);
                const Mat4 fold = (i < snap.plans.size() && snap.plans[i].hasFold) ? snap.plans[i].foldMatrix : Mat4::identity();
                for (u32 s = 0; s < k; ++s) {
                    if (temporal) {
                        const RenderLayer::TemporalSample& ts = layer.temporal[s];
                        mats[s] = ts.m * fold;
                        prm[s] = Vec4{ts.weight, ts.mask.x, ts.mask.y, ts.mask.z};
                    } else {
                        mats[s] = layer.blurMatrices[s] * fold;
                        prm[s] = Vec4{1.0f / static_cast<f32>(k), 0, 0, 0};
                    }
                }
                TextureDesc accDesc = compDesc;
                accDesc.transferSrc = false;
                const FGTexture acc = graph_.create_texture("desfoque de movimento", accDesc);
                struct Cap {
                    Mat4* mats; Vec4* prm; u32 k; PipelineHandle p; FGTexture src; u64 sampler; Rect region;
                    f32 compW; f32 compH;
                } cap{mats, prm, k, *pAdd, draw.texture, draw.sampler, draw.region,
                      static_cast<f32>(snap.compWidth), static_cast<f32>(snap.compHeight)};
                const u32 pass = graph_.add_raster_pass("desfoque de movimento", PassStage::Composite, acc, LoadOp::Clear,
                                                        Vec4{0, 0, 0, 0}, [cap](PassContext& pc) {
                    const Mat4 clip = clip_from_comp(cap.compW, cap.compH);
                    pc.cmds.bind_pipeline(cap.p);
                    pc.cmds.bind_texture(0, pc.texture(cap.src), SamplerHandle{cap.sampler});
                    for (u32 s = 0; s < cap.k; ++s) {
                        LayerPush push;
                        push.clipFromLayer = clip * cap.mats[s];
                        push.region = Vec4{cap.region.x, cap.region.y, cap.region.w, cap.region.h};
                        push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
                        push.params = cap.prm[s];
                        pc.cmds.push_constants(&push, sizeof(push));
                        pc.cmds.draw(6);
                    }
                });
                graph_.read(pass, draw.texture);
                draw.texture = acc;
                draw.region = Rect{0.0f, 0.0f, static_cast<f32>(snap.compWidth), static_cast<f32>(snap.compHeight)};
                draw.compFromLayer = Mat4::identity();
                draw.sampler = shaders_.sampler(CommonSampler::LinearClamp).id;
            }
        }
        draws.push_back(draw);
    }

    // --- Composição: um passe, todas as layers, blend de hardware.
    {
        auto pNormal = shaders_.pipeline(PipelineKey::graphics(
            ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Normal));
        auto pAdd = shaders_.pipeline(PipelineKey::graphics(
            ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Add));
        const u32 count = static_cast<u32>(draws.size());
        CompositeDraw* arr = count ? arena_.alloc_array<CompositeDraw>(count) : nullptr;
        for (u32 i = 0; i < count; ++i) arr[i] = draws[i];
        struct Cap {
            CompositeDraw* draws; u32 count; PipelineHandle normal; PipelineHandle add;
            f32 compW; f32 compH;
        } cap{arr, count, pNormal.ok() ? *pNormal : PipelineHandle{}, pAdd.ok() ? *pAdd : PipelineHandle{},
              static_cast<f32>(snap.compWidth), static_cast<f32>(snap.compHeight)};
        const u32 pass = graph_.add_raster_pass("composicao", PassStage::Composite, comp, LoadOp::Clear,
                                                snap.background, [cap](PassContext& pc) {
            const Mat4 clip = clip_from_comp(cap.compW, cap.compH);
            PipelineHandle bound{};
            for (u32 i = 0; i < cap.count; ++i) {
                const CompositeDraw& d = cap.draws[i];
                // Blend: Normal e Add são de hardware. Os demais modos ainda
                // não têm passe com leitura do destino — caem no Normal (e o
                // painel mostra), em vez de uma aproximação silenciosa.
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
        for (const CompositeDraw& d : draws) graph_.read(pass, d.texture);
    }
}

Status Renderer::render(FrameSnapshot& snap, const RenderSettings& settings,
                        const OffscreenTarget* offscreen, FrameStats& stats,
                        RenderTimings& timings) noexcept {
    if (!backend_) return Status{Errc::InvalidState, "renderer sem backend"};
    const u64 t0 = monotonic_ns();
    heavyScale_ = settings.finalQuality ? 1.0f : settings.heavyScale;

    FrameBegin fb;
    if (const Status s = backend_->begin_frame(fb); !s.ok()) {
        // Sem imagem de swapchain neste vsync (superfície em recriação): os
        // frames de vídeo deste snapshot só são soltos — não houve GPU.
        for (RenderLayer& l : snap.layers) l.source.frame.reset();
        return s;
    }
    const u64 tBegin = monotonic_ns();
    timings.acquireWaitMs = static_cast<f32>(static_cast<f64>(tBegin - t0) * 1e-6);

    // Atlas de glifos: sobe quando ganhou glifo novo (ou foi refeito).
    {
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
        }
    }
    flush_uploads();
    pool_.begin_frame(*backend_, fb.frameNumber);
    graph_.reset();
    arena_.reset();
    draws_.clear();
    lastZeroCopy_ = false;
    framesInFlight_.clear();

    // --- Alvo da composição: resolução de PREVIEW, coordenadas de COMPOSIÇÃO.
    u32 cw = 0, ch = 0;
    if (offscreen && offscreen->texture.valid()) {
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
    const FGTexture comp = (offscreen && offscreen->texture.valid())
                         ? graph_.import_texture("composicao", offscreen->texture, compDesc)
                         : graph_.create_texture("composicao", compDesc);
    currentScenes_ = &snap.scenes;
    if (offscreen && !snap.scenes.empty()) scene3d_.finish_environment(snap.scenes[0].environment);
    compTargetW_ = cw;
    compTargetH_ = ch;

    upload_glyphs(snap);
    compose_layers(snap, comp, compDesc, fb.frameNumber, draws_, 0);
    // A raiz de novo (as pré-composições trocaram o estado em curso).
    currentScenes_ = &snap.scenes;
    currentSnap_ = &snap;
    compTargetW_ = cw;
    compTargetH_ = ch;
    EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat,
                           std::min<u32>(backend_->capabilities().maxTexture2D, 8192));

    // --- Saída.
    if (!offscreen && fb.backbuffer.valid()) {
        TextureDesc bbDesc;
        bbDesc.width = fb.backbufferWidth;
        bbDesc.height = fb.backbufferHeight;
        bbDesc.format = fb.backbufferFormat;
        const FGTexture bb = graph_.import_texture("swapchain", fb.backbuffer, bbDesc);
        auto pOut = shaders_.pipeline(PipelineKey::graphics(ShaderId::composite_layer_vert,
                                                            ShaderId::composite_output_frag, fb.backbufferFormat));
        // A composição encaixada (letterbox) no espaço LÓGICO do display, e a
        // pré-rotação aplicada no clip: o compositor do sistema não precisa
        // girar a imagem (o que custaria um passe a mais por frame, dele).
        const bool swap = fb.rotation == SurfaceRotation::Rotate90 || fb.rotation == SurfaceRotation::Rotate270;
        const f32 dispW = static_cast<f32>(swap ? fb.backbufferHeight : fb.backbufferWidth);
        const f32 dispH = static_cast<f32>(swap ? fb.backbufferWidth : fb.backbufferHeight);
        const f32 compW = static_cast<f32>(snap.compWidth), compH = static_cast<f32>(snap.compHeight);
        const f32 fit = std::min(dispW / compW, dispH / compH) * std::max(0.01f, settings.viewportZoom);
        const f32 ox = (dispW - compW * fit) * 0.5f + settings.viewportPan.x;
        const f32 oy = (dispH - compH * fit) * 0.5f + settings.viewportPan.y;
        Mat4 dispFromComp = Mat4::translation(Vec3{ox, oy, 0}) * Mat4::scale(Vec3{fit, fit, 1});
        Mat4 clip = clip_from_comp(dispW, dispH) * dispFromComp;
        f32 angle = 0.0f;
        switch (fb.rotation) {
            case SurfaceRotation::Rotate90:  angle = 90.0f; break;
            case SurfaceRotation::Rotate180: angle = 180.0f; break;
            case SurfaceRotation::Rotate270: angle = 270.0f; break;
            default: break;
        }
        if (angle != 0.0f) clip = Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, angle * kDeg2Rad)) * clip;

        const Vec4 bg = settings.editorBackground;
        void* ubo = arena_.alloc(16, 16);
        std::memcpy(ubo, &bg, 16);
        struct Cap { PipelineHandle p; FGTexture comp; u64 sampler; Mat4 clip; f32 w, h; f32 dither; void* ubo; }
            cap{pOut.ok() ? *pOut : PipelineHandle{}, comp, shaders_.sampler(CommonSampler::LinearClamp).id,
                clip, compW, compH, settings.dither ? 1.0f : 0.0f, ubo};
        const Vec4 clear{0, 0, 0, 1};
        const u32 pass = graph_.add_raster_pass("saida", PassStage::Output, bb, LoadOp::Clear, clear,
                                                [cap](PassContext& pc) {
            if (!cap.p.valid()) return;
            pc.cmds.bind_pipeline(cap.p);
            LayerPush push;
            push.clipFromLayer = cap.clip;
            push.region = Vec4{0.0f, 0.0f, cap.w, cap.h};
            push.uvRect = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
            push.params = Vec4{cap.dither, 0, 0, 0};
            pc.cmds.bind_texture(0, pc.texture(cap.comp), SamplerHandle{cap.sampler});
            pc.cmds.set_uniforms(cap.ubo, 16);
            pc.cmds.push_constants(&push, sizeof(push));
            pc.cmds.draw(6);
        });
        graph_.read(pass, comp);
        graph_.set_output(bb, ResourceState::Present);
    } else if (offscreen && offscreen->yPlane.valid() && offscreen->uvPlane.valid()) {
        // Export: composição → NV12 na GPU (o encoder recebe planos prontos).
        TextureDesc yd;
        yd.width = cw;
        yd.height = ch;
        yd.format = SurfaceFormat::R8;
        yd.renderTarget = true;
        yd.transferSrc = true;
        TextureDesc uvd = yd;
        uvd.width = cw / 2;
        uvd.height = ch / 2;
        uvd.format = SurfaceFormat::RG8;
        const FGTexture yTex = graph_.import_texture("export-y", offscreen->yPlane, yd);
        const FGTexture uvTex = graph_.import_texture("export-cbcr", offscreen->uvPlane, uvd);
        struct YuvUniforms { Vec4 cfg; Vec4 matrix; };
        const f32 dither = offscreen->encodeDither ? 1.0f : 0.0f;
        const YuvUniforms uy{Vec4{0.0f, dither, 0, 0}, Vec4{0.2126f, 0.0722f, 0, 0}};
        const YuvUniforms uc{Vec4{1.0f, dither, 0, 0}, Vec4{0.2126f, 0.0722f, 0, 0}};
        const u32 py = ctx.fullscreen_pass("export-y", PassStage::Output, yTex, ShaderId::export_yuv_encode_frag,
                                           {PassTexture{comp, {}, CommonSampler::NearestClamp}}, &uy, sizeof(uy));
        const u32 pc = ctx.fullscreen_pass("export-cbcr", PassStage::Output, uvTex, ShaderId::export_yuv_encode_frag,
                                           {PassTexture{comp, {}, CommonSampler::NearestClamp}}, &uc, sizeof(uc));
        if (py != kInvalidIndex) graph_.mark_side_effect(py);
        if (pc != kInvalidIndex) graph_.mark_side_effect(pc);
        graph_.set_output(comp, ResourceState::ShaderRead);
    } else {
        graph_.set_output(comp, ResourceState::ShaderRead);
    }

    Status result = graph_.compile(pool_);
    if (result.ok()) {
        graph_.execute(*fb.commands, settings.gpuTimers);
    } else {
        AUREA_LOG_ERROR("FrameGraph nao compilou: %s", result.message().data());
    }
    graph_.release(pool_);
    pool_.end_frame();

    const u64 tRecorded = monotonic_ns();
    const Status endStatus = backend_->end_frame();
    const u64 tEnd = monotonic_ns();

    // Os frames de vídeo lidos neste frame voltam ao decoder SÓ quando a GPU
    // terminar — nem antes (frame rasgado), nem depois (decoder sem buffer).
    for (FrameRef& fr : framesInFlight_) {
        DecodedFrame* raw = fr.detach();
        if (raw) backend_->defer_until_gpu_done([](void* p) { static_cast<DecodedFrame*>(p)->release(); }, raw);
    }
    framesInFlight_.clear();
    for (RenderLayer& l : snap.layers) l.source.frame.reset();

    collect_resources(fb.frameNumber);

    timings.cpuRecordMs = static_cast<f32>(static_cast<f64>(tRecorded - tBegin) * 1e-6);
    timings.presentMs = static_cast<f32>(static_cast<f64>(tEnd - tRecorded) * 1e-6);
    read_timings(timings);

    stats.passesExecuted = graph_.stats().passesExecuted;
    stats.passesCulled = graph_.stats().passesCulled;
    stats.drawCalls = static_cast<u32>(draws_.size()) + graph_.stats().passesExecuted;
    stats.layersRendered = static_cast<u32>(draws_.size());
    stats.previewWidth = cw;
    stats.previewHeight = ch;
    stats.gpuMs = timings.gpuMeasured ? timings.gpuTotalMs : 0.0f;
    stats.gpuMemoryBytes = backend_->memory_stats().usedBytes;
    ++framesRendered_;

    if (!result.ok()) return result;
    return endStatus;
}

void Renderer::collect_resources(u64 frameNumber) noexcept {
    // Recursos persistentes de layers que sumiram. Checado a cada 2 s — não
    // há pressa, e varrer mapas a cada frame é custo sem ganho.
    if (frameNumber % 120 != 0) return;
    scene3d_.collect(frameNumber);
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
            for (TextureHandle& t : it->second.tex) if (t.valid()) backend_->destroy_texture(t);
            it = flowCache_.erase(it);
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
}

void Renderer::read_timings(RenderTimings& t) noexcept {
    f32 total = 0.0f;
    const u32 n = backend_->read_gpu_timings(timingScratch_.data(),
                                             static_cast<u32>(timingScratch_.size()), &total);
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
