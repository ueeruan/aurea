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

} // namespace

bool is_part3d_effect(u32 effectType) noexcept {
    return effectType == effect_type_id(effect_keys::kPart3DTransform) || effectType == effect_type_id(effect_keys::kPart3DHide)
        || effectType == effect_type_id(effect_keys::kPart3DTexture);
}

void part_nodes(const SceneAsset& asset, std::vector<u32>& out) {
    out.clear();
    for (u32 i = 0; i < asset.nodes.size(); ++i) {
        const Node& node = asset.nodes[i];
        if (node.inScene && node.mesh >= 0 && static_cast<usize>(node.mesh) < asset.meshes.size()) out.push_back(i);
    }
}

void part_names(const SceneAsset& asset, std::vector<std::string>& out) {
    std::vector<u32> parts;
    part_nodes(asset, parts);
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
    for (const auto& effect : layer.effects)
        if (effect.enabled && (effect.type == move || effect.type == hide)) return true;
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
    part_nodes(asset, parts);
    if (parts.empty()) return;
    const EffectTypeId move = effect_type_id(effect_keys::kPart3DTransform);
    const EffectTypeId hide = effect_type_id(effect_keys::kPart3DHide);
    // 100% de posição = o tamanho do objeto (o maior lado da caixa).
    const Vec3 extent = asset.bounds.valid() ? asset.bounds.extent() : Vec3{1, 1, 1};
    const f32 size = std::max({extent.x, extent.y, extent.z, 1e-3f});
    for (const auto& effect : layer.effects) {
        if (!effect.enabled) continue;
        if (effect.type == move) {
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
            c[6] = c[7] = c[8] = scale;
            bool neutral = std::fabs(scale - 1.0f) < 1e-7f;
            for (u32 k = 0; k < 6; ++k) neutral = neutral && std::fabs(c[k]) < 1e-7f;
            if (neutral) continue;
            const i32 mesh = asset.nodes[node].mesh;
            const Vec3 pivot = nodeWorld[node].transform_point(asset.meshes[static_cast<usize>(mesh)].bounds.center());
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
                const i64 oneBased = static_cast<i64>(i) + 1;
                const bool inRange = oneBased >= first && oneBased <= last;
                if (inRange == invert) continue;
                f32& alpha = nodeOpacity[parts[i]];
                alpha = std::clamp(alpha * (1.0f - amount), 0.0f, 1.0f);
            }
        }
    }
}

void collect_part_textures(const SceneAsset& asset, const Layer& layer, f64 localTime, std::vector<PartTexture>& out) {
    out.clear();
    if (!has_part_textures(layer)) return;
    std::vector<u32> parts;
    part_nodes(asset, parts);
    if (parts.empty()) return;
    const EffectTypeId swap = effect_type_id(effect_keys::kPart3DTexture);
    auto put = [&](u32 node, u64 image, f32 repeat) {
        for (PartTexture& t : out) if (t.node == node) { t.image = image; t.repeat = repeat; return; }
        out.push_back(PartTexture{node, image, repeat});
    };
    for (const auto& effect : layer.effects) {
        if (!effect.enabled || effect.type != swap || effect.params.size() < 2) continue;
        const u64 image = effect.params[1].constant.ref;
        if (image == 0) continue;
        const f32 repeat = std::clamp(param_at(layer, effect, 3, localTime, 100.0f, false), 1.0f, 100000.0f) * 0.01f;
        if (param_at(layer, effect, 2, localTime, 0.0f, true) >= 0.5f) {
            for (const u32 node : parts) put(node, image, repeat);
            continue;
        }
        const i32 p = part_index(param_at(layer, effect, 0, localTime, 1.0f, true), parts.size());
        if (p >= 0) put(parts[static_cast<usize>(p)], image, repeat);
    }
}

} // namespace aurea::scene3d
