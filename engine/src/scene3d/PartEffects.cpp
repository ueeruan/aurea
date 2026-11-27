// =============================================================================
//  Aurea / scene3d / PartEffects.cpp — Animar parte 3D, Esconder partes 3D e
//  Trocar textura 3D (ver PartEffects.hpp). Os índices de parâmetro são os de
//  Object3DEffects.cpp e ficam fixos nos projetos salvos.
// =============================================================================
#include "aurea/scene3d/PartEffects.hpp"
#include "aurea/scene3d/SceneAsset.hpp"
#include "aurea/scene3d/Shape3D.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::scene3d {
namespace {

/// Valor do parâmetro `i` do efeito no instante: a trilha (interpolada entre
/// quadros) quando existe, senão o valor parado; `fallback` se não for finito.
f32 param_at(const Layer& layer, const EffectInstance& effect, u32 i, f64 time, f32 fallback, bool stepped) noexcept {
    f32 value = i < effect.params.size() ? effect.params[i].constant.as_float() : fallback;
    if (const Track* track = layer.tracks.find(TrackProperty::EffectParam, effect.id, param_track_key(i, 0))) {
        value = stepped ? track->value_or(FrameIndex{static_cast<i64>(std::floor(time))}, value)
                        : track->value_or_f(time, value);
    }
    return std::isfinite(value) ? value : fallback;
}

/// Parte (1..N, como o painel mostra) → índice em `parts`; −1 = fora.
i32 part_index(f32 oneBased, usize count) noexcept {
    const i64 p = std::lround(oneBased) - 1;
    return p >= 0 && static_cast<usize>(p) < count ? static_cast<i32>(p) : -1;
}

/// Leva o nó e TODOS os descendentes (as rodas presas na carroceria do
/// modelo importado vão junto). `nodeWorld` já está no espaço da cena.
void move_subtree(const SceneAsset& asset, u32 node, const Mat4& delta, std::vector<Mat4>& nodeWorld) {
    std::vector<u32> stack{node};
    u32 guard = 0;
    while (!stack.empty() && ++guard < 1u << 20) {
        const u32 n = stack.back();
        stack.pop_back();
        if (n >= nodeWorld.size()) continue;
        nodeWorld[n] = delta * nodeWorld[n];
        if (n >= asset.nodes.size()) continue;
        for (const i32 child : asset.nodes[n].children)
            if (child >= 0 && static_cast<u32>(child) != n) stack.push_back(static_cast<u32>(child));
    }
}

void space_parts(const SceneAsset& asset, f32 amount, std::vector<Mat4>& nodeWorld) {
    if (amount <= 0.0f) return;

    struct Part { u32 node; Vec3 center; };
    std::vector<Part> parts;
    Aabb bounds, rigBounds;
    for (u32 i = 0; i < asset.nodes.size() && i < nodeWorld.size(); ++i) {
        const Node& node = asset.nodes[i];
        if (!node.inScene || node.mesh < 0 || static_cast<usize>(node.mesh) >= asset.meshes.size()) continue;
        const Aabb posed = asset.meshes[static_cast<usize>(node.mesh)].bounds.transformed(nodeWorld[i]);
        if (!posed.valid()) continue;
        bounds.add(posed);
        if (node.skin >= 0 && static_cast<usize>(node.skin) < asset.skins.size()) rigBounds.add(posed);
        else parts.push_back(Part{i, posed.center()});
    }
    // Meshes sharing an armature move together so joints and skin weights remain intact.
    if (rigBounds.valid()) parts.push_back(Part{kInvalidIndex, rigBounds.center()});
    if (parts.size() < 2) return;
    const Vec3 center = bounds.center();
    const Vec3 extent = bounds.extent();
    const f32 size = std::max({extent.x, extent.y, extent.z, 1e-4f});
    for (usize i = 0; i < parts.size(); ++i) {
        const Part& part = parts[i];
        // Give concentric pieces distinct directions even when their mesh centers coincide.
        const f32 y = 1.0f - 2.0f * (static_cast<f32>(i) + 0.5f) / static_cast<f32>(parts.size());
        const f32 ring = std::sqrt(std::max(0.0f, 1.0f - y * y));
        const f32 angle = static_cast<f32>(i) * 2.39996323f;
        const Vec3 spread{ring * std::cos(angle), y, ring * std::sin(angle)};
        const Vec3 delta = ((part.center - center) * 1.4f + spread * (size * 0.14f)) * amount;
        if (part.node != kInvalidIndex) {
            nodeWorld[part.node].col[3] = nodeWorld[part.node].col[3] + Vec4{delta, 0.0f};
        } else {
            std::vector<bool> move(nodeWorld.size(), false);
            for (usize n = 0; n < asset.nodes.size() && n < move.size(); ++n)
                move[n] = asset.nodes[n].inScene && asset.nodes[n].mesh >= 0 && asset.nodes[n].skin >= 0;
            for (const Skin& skin : asset.skins) for (const i32 joint : skin.joints)
                if (joint >= 0 && static_cast<usize>(joint) < move.size()) move[static_cast<usize>(joint)] = true;
            for (usize n = 0; n < move.size(); ++n)
                if (move[n]) nodeWorld[n].col[3] = nodeWorld[n].col[3] + Vec4{delta, 0.0f};
        }
    }
}

} // namespace

bool is_part3d_effect(u32 effectType) noexcept {
    return effectType == effect_type_id(effect_keys::kPart3DTransform) || effectType == effect_type_id(effect_keys::kPart3DHide)
        || effectType == effect_type_id(effect_keys::kPart3DTexture) || effectType == effect_type_id(effect_keys::kSpacing3D);
}

void part_nodes(const SceneAsset& asset, std::vector<u32>& out, bool includeJoints) {
    out.clear();
    for (u32 i = 0; i < asset.nodes.size(); ++i) {
        const Node& node = asset.nodes[i];
        if (node.inScene && node.mesh >= 0 && static_cast<usize>(node.mesh) < asset.meshes.size()) out.push_back(i);
    }
    if (!includeJoints) return;
    // Existing mesh indices stay stable in saved projects. Append animation
    // joints so a single skinned avatar exposes its articulated parts too.
    for (const Skin& skin : asset.skins) for (i32 joint : skin.joints) {
        if (joint < 0 || static_cast<usize>(joint) >= asset.nodes.size()) continue;
        const u32 node = static_cast<u32>(joint);
        if (asset.nodes[node].inScene && std::find(out.begin(), out.end(), node) == out.end()) out.push_back(node);
    }
}

void part_names(const SceneAsset& asset, std::vector<std::string>& out, bool includeJoints) {
    std::vector<u32> parts;
    part_nodes(asset, parts, includeJoints);
    out.clear();
    out.reserve(parts.size());
    for (const u32 n : parts) {
        std::string name = asset.nodes[n].name;
        if (name.empty() && asset.nodes[n].mesh >= 0) name = asset.meshes[static_cast<usize>(asset.nodes[n].mesh)].name;
        out.push_back(std::move(name));
    }
}

bool has_part_effects(const Layer& layer) noexcept {
    const EffectTypeId move = effect_type_id(effect_keys::kPart3DTransform);
    const EffectTypeId hide = effect_type_id(effect_keys::kPart3DHide);
    const EffectTypeId spacing = effect_type_id(effect_keys::kSpacing3D);
    for (const auto& effect : layer.effects)
        if (effect.enabled && (effect.type == move || effect.type == hide || effect.type == spacing)) return true;
    return false;
}

bool has_part_textures(const Layer& layer) noexcept {
    const EffectTypeId swap = effect_type_id(effect_keys::kPart3DTexture);
    for (const auto& effect : layer.effects)
        if (effect.enabled && effect.type == swap && effect.params.size() > 1 && effect.params[1].constant.ref != 0) return true;
    return false;
}

void apply_part_effects(const SceneAsset& asset, const Layer& layer, f64 localTime,
                        std::vector<Mat4>& nodeWorld, std::vector<f32>& nodeOpacity) {
    if (!has_part_effects(layer)) return;
    std::vector<u32> parts;
    part_nodes(asset, parts, true);
    if (parts.empty()) return;
    const EffectTypeId move = effect_type_id(effect_keys::kPart3DTransform);
    const EffectTypeId hide = effect_type_id(effect_keys::kPart3DHide);
    const EffectTypeId spacing = effect_type_id(effect_keys::kSpacing3D);
    // 100% de posição = o tamanho do objeto (o maior lado da caixa).
    const Vec3 extent = asset.bounds.valid() ? asset.bounds.extent() : Vec3{1, 1, 1};
    const f32 size = std::max({extent.x, extent.y, extent.z, 1e-3f});
    for (const auto& effect : layer.effects) {
        if (!effect.enabled) continue;
        if (effect.type == spacing) {
            space_parts(asset, std::clamp(param_at(layer, effect, 0, localTime, 32.0f, false), 0.0f, 1000.0f) * 0.01f, nodeWorld);
        } else if (effect.type == move) {
            const i32 p = part_index(param_at(layer, effect, 0, localTime, 1.0f, true), parts.size());
            if (p < 0) continue;
            const u32 node = parts[static_cast<usize>(p)];
            if (node >= nodeWorld.size()) continue;
            f32 c[kShape3DChannels];
            for (u32 axis = 0; axis < 3; ++axis) {
                c[axis] = std::clamp(param_at(layer, effect, 1 + axis, localTime, 0.0f, false), -100000.0f, 100000.0f) * 0.01f * size;
                c[3 + axis] = std::clamp(param_at(layer, effect, 4 + axis, localTime, 0.0f, false), -360000.0f, 360000.0f);
            }
            const f32 scale = std::clamp(param_at(layer, effect, 7, localTime, 100.0f, false), 0.0f, 100000.0f) * 0.01f;
            for (u32 axis = 0; axis < 3; ++axis)
                c[6 + axis] = scale * std::clamp(param_at(layer, effect, 11 + axis, localTime, 100.f, false), 0.f, 100000.f) * .01f;
            bool neutral = true;
            for (u32 axis = 0; axis < 3; ++axis) neutral = neutral && std::fabs(c[6 + axis] - 1.f) < 1e-7f;
            for (u32 k = 0; k < 6; ++k) neutral = neutral && std::fabs(c[k]) < 1e-7f;
            if (neutral) continue;
            const i32 mesh = asset.nodes[node].mesh;
            Vec3 center{};
            if (mesh >= 0 && static_cast<usize>(mesh) < asset.meshes.size()) center = asset.meshes[static_cast<usize>(mesh)].bounds.center();
            const Vec3 offset{
                std::clamp(param_at(layer, effect, 8, localTime, 0.f, false), -100000.f, 100000.f) * .01f * size,
                std::clamp(param_at(layer, effect, 9, localTime, 0.f, false), -100000.f, 100000.f) * .01f * size,
                std::clamp(param_at(layer, effect, 10, localTime, 0.f, false), -100000.f, 100000.f) * .01f * size};
            const Vec3 pivot = nodeWorld[node].transform_point(center) + offset;
            move_subtree(asset, node, shape3d_part_matrix(c, pivot), nodeWorld);
        } else if (effect.type == hide) {
            const f32 amount = std::clamp(param_at(layer, effect, 2, localTime, 100.0f, false) * 0.01f, 0.0f, 1.0f);
            if (amount <= 0.0f) continue;
            i64 first = std::lround(param_at(layer, effect, 0, localTime, 1.0f, true));
            i64 last = std::lround(param_at(layer, effect, 1, localTime, 1.0f, true));
            if (first > last) std::swap(first, last);
            const bool invert = param_at(layer, effect, 3, localTime, 0.0f, true) >= 0.5f;
            if (nodeOpacity.size() < asset.nodes.size()) nodeOpacity.resize(asset.nodes.size(), 1.0f);
            for (usize i = 0; i < parts.size(); ++i) {
                if (asset.nodes[parts[i]].mesh < 0) continue;
                const i64 oneBased = static_cast<i64>(i) + 1;
                const bool inRange = oneBased >= first && oneBased <= last;
                if (inRange == invert) continue;
                f32& alpha = nodeOpacity[parts[i]];
                alpha = std::clamp(alpha * (1.0f - amount), 0.0f, 1.0f);
            }
        }
    }
}

f32 part_texture_offset(f32 offsetPct, f32 scrollPctPerSecond, f64 localFrames, f64 fps) noexcept {
    const f64 rate = fps > 0.0 && std::isfinite(fps) ? fps : 30.0;
    f64 v = std::isfinite(offsetPct) ? static_cast<f64>(offsetPct) * 0.01 : 0.0;
    if (std::isfinite(scrollPctPerSecond) && scrollPctPerSecond != 0.0f && std::isfinite(localFrames))
        v += static_cast<f64>(scrollPctPerSecond) * 0.01 * (localFrames / rate);
    // A imagem repete: só a fração conta (e a precisão não se perde com o tempo).
    v -= std::floor(v);
    if (!std::isfinite(v) || v >= 1.0) v = 0.0;
    return static_cast<f32>(v);
}

void collect_part_textures(const SceneAsset& asset, const Layer& layer, f64 localTime, std::vector<PartTexture>& out,
                           f64 fps) {
    out.clear();
    if (!has_part_textures(layer)) return;
    std::vector<u32> parts;
    part_nodes(asset, parts);
    if (parts.empty()) return;
    const EffectTypeId swap = effect_type_id(effect_keys::kPart3DTexture);
    auto put = [&](const PartTexture& value) {
        for (PartTexture& t : out) if (t.node == value.node) { t = value; return; }
        out.push_back(value);
    };
    for (const auto& effect : layer.effects) {
        if (!effect.enabled || effect.type != swap || effect.params.size() < 2) continue;
        const u64 image = effect.params[1].constant.ref;
        if (image == 0) continue;
        PartTexture t;
        t.image = image;
        t.repeat = std::clamp(param_at(layer, effect, 3, localTime, 100.0f, false), 1.0f, 100000.0f) * 0.01f;
        // v76: deslocamento, giro e rolar (efeito antigo sem esses parâmetros: zeros).
        const f32 ox = param_at(layer, effect, kPartTextureOffsetX, localTime, 0.0f, false);
        const f32 oy = param_at(layer, effect, kPartTextureOffsetY, localTime, 0.0f, false);
        const f32 sx = param_at(layer, effect, kPartTextureScrollX, localTime, 0.0f, false);
        const f32 sy = param_at(layer, effect, kPartTextureScrollY, localTime, 0.0f, false);
        if (ox != 0.0f || sx != 0.0f) t.offset.x = part_texture_offset(ox, sx, localTime, fps);
        if (oy != 0.0f || sy != 0.0f) t.offset.y = part_texture_offset(oy, sy, localTime, fps);
        const f32 degrees = param_at(layer, effect, kPartTextureRotation, localTime, 0.0f, false);
        if (std::isfinite(degrees) && degrees != 0.0f) t.rotation = std::fmod(degrees, 360.0f) * kDeg2Rad;
        if (param_at(layer, effect, 2, localTime, 0.0f, true) >= 0.5f) {
            for (const u32 node : parts) { t.node = node; put(t); }
            continue;
        }
        const i32 p = part_index(param_at(layer, effect, 0, localTime, 1.0f, true), parts.size());
        if (p >= 0) { t.node = parts[static_cast<usize>(p)]; put(t); }
    }
}


} // namespace aurea::scene3d
