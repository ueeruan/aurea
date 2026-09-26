#include "aurea/Engine.hpp"
#include "aurea/project/Presets.hpp"
#include <algorithm>
#include <cmath>
namespace aurea {
std::string Engine::save_caption_bundle(u64 layerId, const std::string& name) noexcept {
    std::lock_guard lock(modelMutex_);
    auto* comp = project_ ? current_composition() : nullptr;
    const auto* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || layer->captions.empty() || name.empty() || name.size() > 120) return {};
    json::Writer w;
    w.begin_object().key("schema").value(1).key("minAppVersion").value(2113).key("name").value(name);
    w.key("width").value(comp->width()).key("height").value(comp->height());
    w.key("caption").value(presets::make_caption_preset(name, layer->captionOptions, false));
    const presets::PresetKind kinds[] = {presets::PresetKind::Text, presets::PresetKind::Effects, presets::PresetKind::Animation};
    const char* keys[] = {"text", "effects", "animation"};
    for (u32 i = 0; i < 3; ++i) {
        presets::Preset preset;
        const bool captured = presets::capture(*layer, kinds[i], name, comp->fps(), presets::kTextAll, &effectRegistry_, preset);
        w.key(keys[i]).value(captured ? presets::write(preset, &effectRegistry_) : std::string{});
    }
    const auto& t = layer->transform;
    w.key("transform").begin_array().value(t.position.x / comp->width()).value(t.position.y / comp->height()).value(t.position.z);
    w.value(t.scale.x).value(t.scale.y).value(t.scale.z).value(t.rotation.x).value(t.rotation.y).value(t.rotation.z).value(t.opacity).end_array();
    return w.end_object().str();
}
bool Engine::apply_caption_bundle(u64 layerId, const std::string& data) noexcept {
    if (data.size() > 128 * 1024) return false;
    json::Value root;
    if (!json::parse(data, root) || !root.is_object()) return false;
    const auto* schema = root.get("schema"); const auto* version = root.get("minAppVersion");
    const auto* transform = root.get("transform"); const auto* width = root.get("width"); const auto* height = root.get("height");
    if (!schema || !schema->is_number() || schema->number != 1 || !version || !version->is_number() || version->number > 2113 || !transform || !transform->is_array() || transform->array.size() != 10 || !width || !height || !width->is_number() || !height->is_number() || width->number < 1 || height->number < 1 || width->number > 16384 || height->number > 16384) return false;
    for (const auto& n : transform->array) if (!n.is_number() || !std::isfinite(n.number) || std::abs(n.number) > 10000) return false;
    presets::Preset parsed[4]; bool present[4]{};
    const char* keys[] = {"caption", "text", "effects", "animation"};
    const presets::PresetKind kinds[] = {presets::PresetKind::Caption, presets::PresetKind::Text, presets::PresetKind::Effects, presets::PresetKind::Animation};
    for (u32 i = 0; i < 4; ++i) {
        const auto* item = root.get(keys[i]);
        if (!item || !item->is_string()) return false;
        if (item->string.empty()) { if (i < 2) return false; continue; }
        if (!presets::parse(item->string, parsed[i], &effectRegistry_) || parsed[i].kind != kinds[i]) return false;
        present[i] = true;
    }
    std::lock_guard lock(modelMutex_);
    auto* comp = project_ ? current_composition() : nullptr;
    auto* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || layer->locked || layer->captions.empty()) return false;
    Layer candidate = *layer;
    candidate.captionOptions = parsed[0].caption;
    candidate.effects.clear();
    for (u32 i = 1; i < 4; ++i) if (present[i] && !presets::apply(parsed[i], candidate, 0, 0, comp->fps(), &effectRegistry_)) return false;
    candidate.text.size *= static_cast<f32>(std::min(comp->width(), comp->height()) / std::min(width->number, height->number));
    auto f = [&](u32 i) { return static_cast<f32>(transform->array[i].number); };
    candidate.transform.position = {f(0)*comp->width(), f(1)*comp->height(), f(2)};
    candidate.transform.scale = {f(3), f(4), f(5)};
    candidate.transform.rotation = {f(6), f(7), f(8)};
    candidate.transform.opacity = std::clamp(f(9), 0.f, 1.f);
    history_.before_mutation(*comp, project_->timeline().current(), "aplicar preset de legenda");
    *layer = std::move(candidate); recenter_text(*layer);
    modelRevision_.fetch_add(1, std::memory_order_acq_rel); project_->mark_dirty(); request_render();
    return true;
}
}
