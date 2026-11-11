// =============================================================================
//  Aurea / timeline / LayerWorld.cpp
//
//  As matrizes da camada e a cadeia de pais (movidas do Renderer.cpp sem mudar
//  a conta) + Auto-Orientar e câmera de dois nós (AE animation, timeline v68).
//  Ver LayerWorld.hpp.
// =============================================================================
#include "aurea/timeline/LayerWorld.hpp"

#include "aurea/core/LayerSkew.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/Parameter.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/GridLayout.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/timeline/LayerAnimator.hpp"
#include "aurea/timeline/SpatialPath.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <new>
#include <unordered_map>
#include <vector>

namespace aurea::world {
namespace {

std::atomic<BoneFrameFn> g_boneFrame{nullptr};
std::atomic<CompProjectionFn> g_projection{nullptr};

Mat4 bone_frame(const Composition& comp, const Layer& parent, i32 node, f64 time) noexcept {
    const BoneFrameFn fn = g_boneFrame.load(std::memory_order_acquire);
    return fn ? fn(comp, parent, node, time) : Mat4::identity();
}

// --- AE animation: orientação -------------------------------------------------

/// Posição da camada no espaço do pai (trilhas + caminho curvo), sem os
/// animadores de camada: é o caminho de movimento que o Auto-Orientar segue.
Vec3 path_position(const Layer& l, f64 local) noexcept {
    const TrackSet& t = l.tracks;
    auto s = [&](TrackProperty p, f32 fallback) noexcept {
        const Track* tr = t.find(p);
        return tr ? sample_frac(*tr, local, fallback) : fallback;
    };
    Vec3 pos{s(TrackProperty::PositionX, l.transform.position.x), s(TrackProperty::PositionY, l.transform.position.y),
             s(TrackProperty::PositionZ, l.transform.position.z)};
    if (Vec3 sp; spatial_position(t, l.positionTangents, l.transform.position, local, sp)) pos = sp;
    return pos;
}

/// Direção do caminho de movimento no instante (diferença central; parado
/// num key ou fora dos keys, a direção mais perto adiante/atrás, como o AE
/// usa a tangente do key). Falso = a camada não anda.
bool path_tangent(const Layer& l, f64 local, Vec3& out) noexcept {
    constexpr f32 kTiny = 1e-4f;
    for (const f64 h : {0.5, 2.0, 8.0, 32.0}) {
        const Vec3 d = path_position(l, local + h) - path_position(l, local - h);
        if (d.length() > kTiny) { out = d; return true; }
    }
    const Vec3 here = path_position(l, local);
    for (const f64 h : {1.0, 4.0, 16.0, 64.0, 256.0}) {
        const Vec3 ahead = path_position(l, local + h) - here;
        if (ahead.length() > kTiny) { out = ahead; return true; }
        const Vec3 behind = here - path_position(l, local - h);
        if (behind.length() > kTiny) { out = behind; return true; }
    }
    return false;
}

/// Rotação que leva +X da camada para a direção `d` (giro em Z = atan2 no
/// plano, inclinação pelo Y), a mesma do 2D quando o caminho é plano.
Mat4 orient_along(Vec3 d) noexcept {
    const f32 yaw = std::atan2(d.y, d.x);
    const f32 pitch = std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y));
    return Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, yaw) * Quat::from_axis_angle(Vec3{0, 1, 0}, -pitch));
}

/// Rotação cujo +Z aponta para `forward`, sem rolar (X fica no plano XZ; Y da
/// tela para baixo). É a mira da câmera de dois nós e a camada de frente para
/// a câmera.
Mat4 aim_z(Vec3 forward) noexcept {
    Vec3 z = forward.normalized();
    if (z.length_sq() < kEpsilon) return Mat4::identity();
    Vec3 x = Vec3{0, 1, 0}.cross(z);
    if (x.length_sq() < 1e-8f) x = Vec3{1, 0, 0};
    x = x.normalized();
    const Vec3 y = z.cross(x).normalized();
    Mat4 m;
    m.col[0] = Vec4{x.x, x.y, x.z, 0};
    m.col[1] = Vec4{y.x, y.y, y.z, 0};
    m.col[2] = Vec4{z.x, z.y, z.z, 0};
    return m;
}

/// O Auto-Orientar "para a câmera" lê a câmera, que pode depender de pais com
/// a mesma opção: dentro dessa leitura a opção não vale (sem recursão).
thread_local u32 tCameraProbe = 0;

} // namespace

void set_bone_frame_provider(BoneFrameFn fn) noexcept { g_boneFrame.store(fn, std::memory_order_release); }
void set_comp_projection_provider(CompProjectionFn fn) noexcept { g_projection.store(fn, std::memory_order_release); }

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
    Vec3 pos{s(TrackProperty::PositionX, l.transform.position.x) + an.translate.x,
             s(TrackProperty::PositionY, l.transform.position.y) + an.translate.y, 0.0f};
    if (Vec3 sp; spatial_position(t, l.positionTangents, l.transform.position, local, sp)) {
        pos.x = sp.x + an.translate.x; pos.y = sp.y + an.translate.y;
    }
    const Vec3 scale{s(TrackProperty::ScaleX, l.transform.scale.x) * an.scale.x,
                     s(TrackProperty::ScaleY, l.transform.scale.y) * an.scale.y, 1.0f};
    f32 rot = (s(TrackProperty::RotationZ, l.transform.rotation.z) + an.rotation.z) * kDeg2Rad;
    // AE animation (v68): Auto-Orientar ao longo do caminho — o X da camada
    // segue a tangente do caminho de movimento (somado à rotação).
    if (l.autoOrient == 1) {
        if (Vec3 d; path_tangent(l, local, d)) rot += std::atan2(d.y, d.x);
    }
    const Vec3 anchor{s(TrackProperty::AnchorX, l.transform.anchor.x),
                      s(TrackProperty::AnchorY, l.transform.anchor.y), 0.0f};
    // Omega aparência (P0-6): inclinação entre a escala e a âncora (LayerSkew.hpp).
    const f32 kx = s(TrackProperty::SkewX, l.transform.skewX), ky = s(TrackProperty::SkewY, l.transform.skewY);
    if (has_skew(kx, ky))
        return Mat4::translation(pos) * Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, rot))
             * Mat4::scale(scale) * skew_matrix(kx, ky) * Mat4::translation(-anchor);
    return Mat4::translation(pos) * Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, rot))
         * Mat4::scale(scale) * Mat4::translation(-anchor);
}

/// Transform 3D completo da layer (posição/rotação/escala/âncora em XYZ),
/// num tempo local fracionário (inteiro = o próprio quadro).
Mat4 layer_matrix_3d_frac(const Layer& l, f64 local, f64 fps, const Vec3* cameraInParent) noexcept {
    const TrackSet& t = l.tracks;
    auto s = [&](TrackProperty p, f32 fallback) noexcept {
        const Track* tr = t.find(p);
        return tr ? sample_frac(*tr, local, fallback) : fallback;
    };
    const layeranim::Offset an = layeranim::whole_offset(l, local, fps);
    Vec3 pos{s(TrackProperty::PositionX, l.transform.position.x) + an.translate.x,
             s(TrackProperty::PositionY, l.transform.position.y) + an.translate.y,
             s(TrackProperty::PositionZ, l.transform.position.z)};
    if (Vec3 sp; spatial_position(t, l.positionTangents, l.transform.position, local, sp)) {
        pos = Vec3{sp.x + an.translate.x, sp.y + an.translate.y, sp.z};
    }
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
    Mat4 orientation = Mat4::from_quat(Quat::from_euler_zyx(rot.x, rot.y, rot.z));
    // AE animation (v68): a orientação automática vem ANTES da rotação da
    // camada (a rotação gira por cima, como a Orientation + Rotation do AE).
    if (l.kind == LayerKind::Camera && l.camera.twoNode) {
        // Câmera de dois nós: mira no Ponto de Interesse (espaço do pai).
        const Vec3 poi{s(TrackProperty::CameraPoiX, l.camera.poi.x), s(TrackProperty::CameraPoiY, l.camera.poi.y),
                       s(TrackProperty::CameraPoiZ, l.camera.poi.z)};
        orientation = aim_z(poi - pos) * orientation;
    } else if (l.autoOrient == 1) {
        // Câmera e luz olham PARA ONDE andam (o eixo Z); as outras camadas
        // levam o eixo X pela tangente.
        const bool aims = l.kind == LayerKind::Camera || l.kind == LayerKind::Light;
        if (Vec3 d; path_tangent(l, local, d)) orientation = (aims ? aim_z(d) : orient_along(d)) * orientation;
    } else if (l.autoOrient == 2 && cameraInParent && l.kind != LayerKind::Camera && l.kind != LayerKind::Light) {
        // De frente para a câmera: o +Z da camada aponta da câmera para ela.
        orientation = aim_z(pos - *cameraInParent) * orientation;
    }
    // Omega aparência (P0-6): a mesma inclinação no 3D (no plano XY da camada).
    const f32 kx = s(TrackProperty::SkewX, l.transform.skewX), ky = s(TrackProperty::SkewY, l.transform.skewY);
    if (has_skew(kx, ky))
        return Mat4::translation(pos) * orientation * Mat4::scale(scale)
             * skew_matrix(kx, ky) * Mat4::translation(-anchor);
    return Mat4::translation(pos) * orientation * Mat4::scale(scale)
         * Mat4::translation(-anchor);
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
    // Omega grid: a cadeia de pais não tem teto; o limite só corta um ciclo.
    const u32 chainLimit = std::max(comp.layers().count(), comp.order().size()) + 1;
    for (u32 depth = 0; p && depth <= chainLimit; ++depth) {
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

namespace {
// Omega grid (0.3.5): cache do mundo dos PAIS durante um quadro.
// A cadeia de pais não tem mais teto (nulo → nulo → …, grade dentro de grade):
// sem cache, N camadas sob uma cadeia de profundidade D custariam N × D
// matrizes por quadro. Com o `WorldMatrixCacheScope` aberto (Renderer::prepare,
// preview e export), o mundo de cada pai é calculado uma vez por (composição,
// camada, instante, 2D/3D) e o quadro custa N + D. Fora do escopo (edições,
// consultas da UI, testes) não há cache: o modelo pode mudar entre chamadas.
// Só os pais vão para o cache (ponteiros da composição, nunca cópias) e só
// cadeias sem "Parenting Helper" abaixo da raiz (a conta simples).
struct WorldCacheKey {
    const Composition* comp;
    const Layer* layer;
    f64 time;
    bool threeD;
    bool operator==(const WorldCacheKey& o) const noexcept {
        return comp == o.comp && layer == o.layer && time == o.time && threeD == o.threeD;
    }
};
struct WorldCacheHash {
    usize operator()(const WorldCacheKey& k) const noexcept {
        usize h = std::hash<const void*>{}(k.comp);
        auto mix = [&h](usize v) { h ^= v + static_cast<usize>(0x9e3779b9u) + (h << 6) + (h >> 2); };
        mix(std::hash<const void*>{}(k.layer));
        mix(std::hash<f64>{}(k.time));
        mix(k.threeD ? 1u : 0u);
        return h;
    }
};
struct WorldCache {
    std::unordered_map<WorldCacheKey, Mat4, WorldCacheHash> worlds;
};
thread_local WorldCache* tWorldCache = nullptr;

/// A cadeia (camada, pai, avô, …) sem teto: 24 no lugar, o resto no heap.
struct LayerChain {
    const Layer* inl[24]{};
    std::vector<const Layer*> more;
    u32 n = 0;
    void push(const Layer* p) { if (n < 24) inl[n] = p; else more.push_back(p); ++n; }
    const Layer* operator[](u32 i) const noexcept { return i < 24 ? inl[i] : more[i - 24]; }
    bool contains(const Layer* p) const noexcept { for (u32 i = 0; i < n; ++i) if ((*this)[i] == p) return true; return false; }
    void clear() noexcept { n = 0; more.clear(); }
};

/// A câmera ativa no espaço do pai cujo mundo é `parentWorld`.
bool camera_in_parent(const Composition& comp, f64 time, const Mat4& parentWorld, Vec3& out) noexcept {
    Mat4 inv;
    if (!invert(parentWorld, inv)) return false;
    out = inv.transform_point(active_camera_position(comp, time));
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}
} // namespace

WorldMatrixCacheScope::WorldMatrixCacheScope() noexcept {
    if (tWorldCache) return;
    auto* cache = new (std::nothrow) WorldCache;
    tWorldCache = cache;
    owned_ = cache;
}

WorldMatrixCacheScope::~WorldMatrixCacheScope() {
    if (!owned_) return;
    tWorldCache = nullptr;
    delete static_cast<WorldCache*>(owned_);
}

Mat4 world_chain_frac(const Composition& comp, const Layer& l, f64 time, bool threeD, f64 parentTime) noexcept {
    auto parentOf = [&](const Layer* p) -> const Layer* { return p->parent.valid() ? comp.layer(p->parent) : nullptr; };
    // `parentWorld`: o mundo do pai já montado (o Auto-Orientar "para a câmera"
    // leva a câmera para o espaço do pai).
    auto ownMatrix = [&](const Layer& node, const Mat4& parentWorld) {
        const f64 local = (&node == &l ? time : parentTime) - static_cast<f64>(node.start.value) + static_cast<f64>(node.offset.value);
        const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
        Vec3 camera;
        const bool facing = threeD && node.autoOrient == 2 && tCameraProbe == 0
            && node.kind != LayerKind::Camera && node.kind != LayerKind::Light
            && camera_in_parent(comp, &node == &l ? time : parentTime, parentWorld, camera);
        const Mat4 own = threeD ? layer_matrix_3d_frac(node, local, fps, facing ? &camera : nullptr) : layer_matrix_frac(node, local, fps);
        const Mat4 bound = node.hasParentBasis ? node.parentBasis * own : own;
        const Mat4 m = grid::evaluate(comp,node,&node == &l ? time : parentTime).matrix * bound;
        // 0.5.2: preso a um osso do pai (modelo 3D): o osso entra entre o pai e a camada.
        if (threeD && node.parentBone >= 0)
            if (const Layer* p = parentOf(&node)) return bone_frame(comp, *p, node.parentBone, parentTime) * m;
        return m;
    };
    auto hasHelper = [](const Layer& node) {
        for (const auto& effect : node.effects)
            if (effect.enabled && effect.type == effect_type_id(effect_keys::kParentingHelper)) return true;
        return false;
    };
    // Projects normally reject cycles on edit/load; retain a finite fallback
    // (the chain stops at the first repeated layer). Without a cycle the chain
    // has at most as many layers as the composition.
    auto collectAll = [&](LayerChain& chain) {
        chain.clear();
        for (const Layer* p = &l; p && !chain.contains(p); p = parentOf(p)) chain.push(p);
    };
    WorldCache* cache = tWorldCache;
    const u32 limit = std::max(comp.layers().count(), comp.order().size()) + 1;
    LayerChain chain;
    const Mat4* top = nullptr;   // mundo já pronto do primeiro pai fora de `chain`
    bool cyclic = false;
    for (const Layer* p = &l; p; p = parentOf(p)) {
        if (chain.n > limit) { cyclic = true; break; }
        if (cache && chain.n > 0) {
            const auto it = cache->worlds.find(WorldCacheKey{&comp, p, parentTime, threeD});
            if (it != cache->worlds.end()) { top = &it->second; break; }
        }
        chain.push(p);
    }
    if (cyclic) { collectAll(chain); top = nullptr; cache = nullptr; }
    // Parenting Helper em qualquer nó abaixo da raiz muda a conta. Um pai do
    // cache só entrou lá sem Helper dele até abaixo da raiz.
    bool helper = false;
    const u32 checked = top ? chain.n : (chain.n ? chain.n - 1 : 0);
    for (u32 i = 0; i < checked; ++i) helper |= hasHelper(*chain[i]);
    if (!helper) {
        Mat4 world = top ? *top : Mat4::identity();
        for (u32 i = chain.n; i-- > 1;) {   // os pais, da raiz para baixo
            world = world * ownMatrix(*chain[i], world);
            if (cache) cache->worlds.emplace(WorldCacheKey{&comp, chain[i], parentTime, threeD}, world);
        }
        return world * ownMatrix(l, world);
    }
    if (top) collectAll(chain);   // a conta do Helper precisa da cadeia inteira
    Mat4 world;
    f32 continuousZ = 0.f;
    u32 count = chain.n;
    while (count) {
        const Layer& node = *chain[--count];
        const f64 local = (&node == &l ? time : parentTime) - static_cast<f64>(node.start.value) + static_cast<f64>(node.offset.value);
        const Mat4 own = ownMatrix(node, world);
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
Mat4 layer_world_frac(const Composition& comp, const Layer& l, f64 time) noexcept {
    const FrameIndex frame{static_cast<i64>(std::floor(time))};
    return wants_3d(comp, l, frame) ? world_3d_frac(comp, l, time) : world_2d_frac(comp, l, time);
}

Vec3 active_camera_position(const Composition& comp, f64 time) noexcept {
    // A mesma escolha do render (Renderer: camera_for_frac_own): a câmera
    // visível mais de CIMA no instante, com um quadro de folga nas bordas.
    auto containsFrac = [](const Layer& l, f64 timeF) {
        const i64 t = static_cast<i64>(std::floor(timeF));
        if (l.contains_time(FrameIndex{t})) return true;
        return l.end.value > l.start.value && (t == l.start.value - 1 || t == l.end.value);
    };
    const auto& order = comp.order();
    for (u32 i = order.size(); i-- > 0;) {
        const Layer* c = comp.layer(order.at(i));
        if (!c || !c->visible || c->kind != LayerKind::Camera || !c->camera.active || !containsFrac(*c, time)) continue;
        ++tCameraProbe;
        const Mat4 wm = world_3d_frac(comp, *c, time);
        --tCameraProbe;
        return Vec3{wm.col[3].x, wm.col[3].y, wm.col[3].z};
    }
    // Câmera padrão (scene3d::default_camera): no centro, a 1,2 × a altura.
    const f32 h = static_cast<f32>(std::max(1u, comp.height()));
    return Vec3{static_cast<f32>(comp.width()) * 0.5f, h * 0.5f, -h * 1.2f};
}

Mat4 comp_from_world(const Composition& comp, f64 time) noexcept {
    const CompProjectionFn fn = g_projection.load(std::memory_order_acquire);
    return fn ? fn(comp, time) : Mat4::identity();
}

bool invert(const Mat4& m, Mat4& out) noexcept {
    // Cofatores em f64 (a matriz da camada mistura px e frações pequenas).
    f64 a[16];
    for (int c = 0; c < 4; ++c) {
        a[c * 4 + 0] = m.col[c].x; a[c * 4 + 1] = m.col[c].y; a[c * 4 + 2] = m.col[c].z; a[c * 4 + 3] = m.col[c].w;
    }
    f64 inv[16];
    inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
    inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
    inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
    inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
    inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
    inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
    inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
    inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
    inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
    inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
    inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
    inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
    inv[3] = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
    inv[7] = a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
    inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] - a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
    inv[15] = a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] + a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];
    const f64 det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (!std::isfinite(det) || std::fabs(det) < 1e-18) { out = Mat4::identity(); return false; }
    const f64 r = 1.0 / det;
    for (int c = 0; c < 4; ++c) {
        out.col[c] = Vec4{static_cast<f32>(inv[c * 4 + 0] * r), static_cast<f32>(inv[c * 4 + 1] * r),
                          static_cast<f32>(inv[c * 4 + 2] * r), static_cast<f32>(inv[c * 4 + 3] * r)};
    }
    return true;
}

} // namespace aurea::world
