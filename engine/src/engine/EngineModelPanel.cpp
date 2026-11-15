// =============================================================================
//  Aurea / engine / EngineModelPanel.cpp
//
//  PAINEL DO MODELO 3D (13/11/2026, conceito aprovado pelo dono: um modelo 3D
//  é UMA camada e o painel dele é simples — Material · Textura · Animação).
//  As interfaces (Android e iOS) só chamam; a regra mora aqui:
//
//  - MATERIAL: um visual para o modelo inteiro (Original, Plástico, Fosco,
//    Metal, Cromo, Ouro, Vidro e, na v76, Neon) sobre o caminho de ajuste de
//    material que já existe (MaterialOverride: cor, opacidade, metal,
//    rugosidade e, na v76, a EMISSÃO — cor e força — o que o renderer desenha
//    por instância). Os deslizantes do modelo inteiro usam
//    set_material_param(kAllMaterials) e toggle_material_keys.
//  - POSIÇÃO (v76): Assentar no chão, Centralizar e Ajustar à tela —
//    place_model_layer em Engine.cpp (usa a caixa da cena 3D de lá).
//  - ANIMAÇÃO: Evolução do clipe (0..1, com keyframe — a trilha
//    TrackProperty::ModelAnimProgress), Repetir e Velocidade. A conta do tempo
//    do clipe é model_clip_seconds (Layer.hpp), a mesma da prévia, do export,
//    do rig e do desfoque de movimento. v76: Começar em, De trás para frente,
//    Ida e volta e a Velocidade com keyframe (TrackProperty::ModelAnimSpeed).
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/scene3d/MaterialPreview.hpp"
#include "aurea/scene3d/SceneAsset.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace {

/// Um visual do modelo: os canais que ele escreve (bits 0..9 = R, G, B,
/// opacidade, metal, rugosidade, emissão R, G, B e força; lineares, como o
/// glTF) e os valores.
struct ModelLook {
    u32 mask;
    f32 v[kMaterialParamCount];
};

constexpr u32 kRgb = 1u | 2u | 4u, kAlpha = 8u, kMetal = 16u, kRough = 32u, kGlow = kMaterialEmissionMask;

/// A ordem é contrato com as duas interfaces (as bolas do painel).
constexpr ModelLook kLooks[Engine::kModelMaterialPresets] = {
    {0u, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},                                         // 0 Original: os materiais do arquivo
    {kMetal | kRough, {0, 0, 0, 0, 0.0f, 0.35f, 0, 0, 0, 0}},                     // 1 Plástico: a cor do arquivo, brilho suave
    {kMetal | kRough, {0, 0, 0, 0, 0.0f, 1.0f, 0, 0, 0, 0}},                      // 2 Fosco
    {kMetal | kRough, {0, 0, 0, 0, 1.0f, 0.35f, 0, 0, 0, 0}},                     // 3 Metal: tingido pela cor do arquivo
    {kRgb | kMetal | kRough, {0.95f, 0.95f, 0.95f, 0, 1.0f, 0.05f, 0, 0, 0, 0}},   // 4 Cromo
    {kRgb | kMetal | kRough, {1.0f, 0.766f, 0.336f, 0, 1.0f, 0.22f, 0, 0, 0, 0}},  // 5 Ouro (F0 linear do ouro)
    {kAlpha | kMetal | kRough, {0, 0, 0, 0.35f, 0.0f, 0.04f, 0, 0, 0, 0}},        // 6 Vidro: transparente e liso
    // 7 Neon (v76): corpo escuro e liso e luz própria azul-ciano saturada (força
    // 1,2: mais que isso o tone map desbota para quase branco) — com o bloom da
    // cena, o modelo brilha como um tubo de neon e o reflexo ainda dá forma.
    {kRgb | kMetal | kRough | kGlow, {0.02f, 0.05f, 0.08f, 0, 0.0f, 0.25f, 0.05f, 0.6f, 1.0f, 1.2f}},
};

f32 source_field(const scene3d::Material& m, u32 p) noexcept {
    switch (p) {
        case 0: return m.baseColor.x; case 1: return m.baseColor.y; case 2: return m.baseColor.z;
        case 3: return m.baseColor.w; case 4: return m.metallic; case 5: return m.roughness;
        case 6: return m.emissive.x; case 7: return m.emissive.y; case 8: return m.emissive.z;
        default: return m.emissiveStrength;
    }
}

MaterialOverride* find_override(Layer& l, u32 material) noexcept {
    for (MaterialOverride& o : l.model.materials) if (o.materialIndex == material) return &o;
    return nullptr;
}

const MaterialOverride* find_override(const Layer& l, u32 material) noexcept {
    for (const MaterialOverride& o : l.model.materials) if (o.materialIndex == material) return &o;
    return nullptr;
}

MaterialOverride& ensure_override(Layer& l, u32 material, const scene3d::Material& source) {
    if (MaterialOverride* o = find_override(l, material)) return *o;
    MaterialOverride o;
    o.materialIndex = material;
    o.baseColor = source.baseColor;
    o.metallic = source.metallic;
    o.roughness = source.roughness;
    o.emissive = source.emissive;
    o.emissiveStrength = source.emissiveStrength;
    l.model.materials.push_back(o);
    return l.model.materials.back();
}

/// O valor do instante (o que query_materials mostra e o renderer desenha):
/// arquivo → ajuste da camada → keyframe.
f32 material_value(const Layer& l, const scene3d::Material& source, u32 material, u32 p, FrameIndex local) noexcept {
    f32 v = source_field(source, p);
    if (const MaterialOverride* o = find_override(l, material); o && (o->mask & (1u << p))) v = material_override_field(*o, p);
    if (const Track* t = l.tracks.find(TrackProperty::MaterialParam, material, p)) v = t->value_or(local, v);
    return std::clamp(v, 0.0f, material_param_max(p));
}

/// Grava `v` no canal: com keyframe na trilha, o keyframe no cabeçote (a regra
/// de set_material_param); senão, o ajuste parado.
void write_material(Layer& l, const scene3d::Material& source, u32 material, u32 p, f32 v, FrameIndex local) {
    Track* t = l.tracks.find(TrackProperty::MaterialParam, material, p);
    if (t && t->has_expression()) return;
    if (t && !t->keys.empty()) {
        if (const u32 at = t->find_exact(local); at != kInvalidIndex) t->keys[at].value = v;
        else (void)t->set(local, v, Interpolation::Linear);
    }
    MaterialOverride& o = ensure_override(l, material, source);
    o.mask |= 1u << p;
    set_material_override_field(o, p, v);
}

/// Evolução (0..1) que a prévia mostra no instante local; 0 sem clipe.
f32 progress_at(const Layer& l, const scene3d::SceneAsset* asset, FrameIndex local, f64 fps) noexcept {
    const i32 clip = l.model.animationClip;
    if (!asset || clip < 0 || clip >= static_cast<i32>(asset->animations.size())) return 0.0f;
    const f32 d = asset->animations[static_cast<usize>(clip)].duration;
    if (!(d > 0.0f) || !std::isfinite(d)) return 0.0f;
    return std::clamp(model_clip_seconds(l, d, static_cast<f64>(local.value), fps) / d, 0.0f, 1.0f);
}

} // namespace

// -----------------------------------------------------------------------------
// Animação
// -----------------------------------------------------------------------------

bool Engine::set_model_animation_param(u64 layerId, u32 param, f32 value) noexcept {
    if (param > 5 || !std::isfinite(value)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D || l->locked) return false;
    Track* track = l->tracks.find(TrackProperty::ModelAnimProgress);
    const CompositionId current = project_->timeline().current();
    switch (param) {
        case 0: {
            if (value < 0.0f) {
                // "Tocar sozinho": tira o valor parado e os keyframes da Evolução.
                if (l->model.animProgress < 0.0f && !track) return true;
                history_.before_mutation(*comp, current, "animação tocando sozinha");
                l->model.animProgress = -1.0f;
                l->tracks.remove_if([](const Track& t) { return t.property == TrackProperty::ModelAnimProgress; });
                break;
            }
            if (track && track->has_expression()) return false;
            const f32 v = std::clamp(value, 0.0f, 1.0f);
            history_.before_mutation(*comp, current, "evolução da animação");
            if (track && !track->keys.empty()) {
                const FrameIndex local = l->local_time(editing_frame_locked());
                if (const u32 at = track->find_exact(local); at != kInvalidIndex) track->keys[at].value = v;
                else (void)track->set(local, v, Interpolation::Linear);
            } else {
                l->model.animProgress = v;
                if (track) track->staticValue = v;
            }
            break;
        }
        case 1: {
            const bool on = value >= 0.5f;
            if (l->model.animLoop == on) return true;
            history_.before_mutation(*comp, current, on ? "repetir animação" : "não repetir animação");
            l->model.animLoop = on;
            break;
        }
        case 2: {
            const f32 v = std::clamp(value, 0.05f, 16.0f);
            Track* speed = l->tracks.find(TrackProperty::ModelAnimSpeed);
            if (speed && speed->has_expression()) return false;
            if (speed && !speed->keys.empty()) {
                // Velocidade com keyframe (v76): o valor vira (ou troca) o keyframe no cabeçote.
                const FrameIndex local = l->local_time(editing_frame_locked());
                history_.before_mutation(*comp, current, "velocidade da animação");
                if (const u32 at = speed->find_exact(local); at != kInvalidIndex) speed->keys[at].value = v;
                else (void)speed->set(local, v, Interpolation::Linear);
                break;
            }
            if (std::fabs(l->model.timeScale - v) < 1e-6f) return true;
            history_.before_mutation(*comp, current, "velocidade da animação");
            l->model.timeScale = v;
            if (speed) speed->staticValue = v;
            break;
        }
        case 3: {
            // Começar em (v76): fração do clipe no início da camada.
            const f32 v = std::clamp(value, 0.0f, 1.0f);
            if (std::fabs(l->model.animStart - v) < 1e-6f) return true;
            history_.before_mutation(*comp, current, "começo da animação");
            l->model.animStart = v;
            break;
        }
        case 4: {
            const bool on = value >= 0.5f;
            if (l->model.animReverse == on) return true;
            history_.before_mutation(*comp, current, on ? "animação de trás para frente" : "animação para frente");
            l->model.animReverse = on;
            break;
        }
        default: {
            const bool on = value >= 0.5f;
            if (l->model.animPingPong == on) return true;
            history_.before_mutation(*comp, current, on ? "animação ida e volta" : "animação só de ida");
            l->model.animPingPong = on;
            break;
        }
    }
    bump_model_revision_locked();
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::toggle_model_animation_param_key(u64 layerId, u32 param) noexcept {
    if (param == 0) return toggle_model_animation_key(layerId);
    if (param != 2) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D || l->locked) return false;
    Track* track = l->tracks.find(TrackProperty::ModelAnimSpeed);
    if (track && track->has_expression()) return false;
    const FrameIndex local = l->local_time(editing_frame_locked());
    if (track && track->find_exact(local) != kInvalidIndex) {
        history_.before_mutation(*comp, project_->timeline().current(), "tirar keyframe da velocidade");
        // O último keyframe que sai deixa a velocidade parada nele (como as outras propriedades).
        if (track->keys.size() == 1) l->model.timeScale = std::clamp(track->keys.front().value, 0.05f, 16.0f);
        (void)track->remove(local);
        if (track->keys.empty()) l->tracks.remove_if([](const Track& t) { return t.property == TrackProperty::ModelAnimSpeed; });
    } else {
        // O valor do keyframe novo = a velocidade que a prévia usa ali.
        const f32 v = track && !track->keys.empty()
            ? static_cast<f32>(Layer::speed_value(*track, static_cast<f64>(local.value))) : l->model.timeScale;
        history_.before_mutation(*comp, project_->timeline().current(), "keyframe da velocidade");
        Track& t = l->tracks.get_or_create(TrackProperty::ModelAnimSpeed, kInvalidIndex, 0);
        t.staticValue = l->model.timeScale;
        (void)t.set(local, std::clamp(v, 0.05f, 16.0f), Interpolation::Linear);
    }
    bump_model_revision_locked();
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::toggle_model_animation_key(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D || l->locked) return false;
    Track* track = l->tracks.find(TrackProperty::ModelAnimProgress);
    if (track && track->has_expression()) return false;
    const FrameIndex local = l->local_time(editing_frame_locked());
    const auto found = models_.find(l->model.scene.pack());
    const scene3d::SceneAsset* asset = found != models_.end() ? found->second.get() : nullptr;
    const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
    if (track && track->find_exact(local) != kInvalidIndex) {
        history_.before_mutation(*comp, project_->timeline().current(), "tirar keyframe da evolução");
        // Tirar o último keyframe deixa a pose parada onde ele estava (como as
        // outras propriedades); "Tocar sozinho" volta ao clipe correndo.
        if (track->keys.size() == 1) l->model.animProgress = std::clamp(track->keys.front().value, 0.0f, 1.0f);
        (void)track->remove(local);
    } else {
        const f32 v = progress_at(*l, asset, local, fps);
        history_.before_mutation(*comp, project_->timeline().current(), "keyframe da evolução");
        Track& t = l->tracks.get_or_create(TrackProperty::ModelAnimProgress, kInvalidIndex, 0);
        t.staticValue = l->model.animProgress;
        (void)t.set(local, v, Interpolation::Linear);
    }
    bump_model_revision_locked();
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_model_animation(u64 layerId, f32* out, u32 capacity) noexcept {
    if (!out || capacity < kModelAnimationValues) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    InlineEditScope inlineScope(*this, layerId);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D) return false;
    const auto found = models_.find(l->model.scene.pack());
    const scene3d::SceneAsset* asset = found != models_.end() ? found->second.get() : nullptr;
    const i32 clip = l->model.animationClip;
    const bool valid = asset && clip >= 0 && clip < static_cast<i32>(asset->animations.size());
    const FrameIndex local = l->local_time(editing_frame_locked());
    const Track* track = l->tracks.find(TrackProperty::ModelAnimProgress);
    const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
    out[0] = valid ? static_cast<f32>(clip) : -1.0f;
    out[1] = valid ? asset->animations[static_cast<usize>(clip)].duration : 0.0f;
    out[2] = l->model.animProgress >= 0.0f ? l->model.animProgress : -1.0f;
    out[3] = progress_at(*l, asset, local, fps);
    out[4] = l->model.animLoop ? 1.0f : 0.0f;
    out[5] = l->model.timeScale;
    out[6] = track && !track->keys.empty() ? 1.0f : 0.0f;
    out[7] = track && track->find_exact(local) != kInvalidIndex ? 1.0f : 0.0f;
    if (capacity < kModelAnimationValuesV76) return true;
    // v76: Começar em, De trás para frente, Ida e volta e a Velocidade com keyframe.
    const Track* speed = l->tracks.find(TrackProperty::ModelAnimSpeed);
    const bool speedKeyed = speed && !speed->keys.empty();
    out[8] = l->model.animStart;
    out[9] = l->model.animReverse ? 1.0f : 0.0f;
    out[10] = l->model.animPingPong ? 1.0f : 0.0f;
    out[11] = speedKeyed ? 1.0f : 0.0f;
    out[12] = speed && speed->find_exact(local) != kInvalidIndex ? 1.0f : 0.0f;
    out[13] = speedKeyed ? static_cast<f32>(Layer::speed_value(*speed, static_cast<f64>(local.value))) : l->model.timeScale;
    return true;
}

bool Engine::query_material_emission(u64 layerId, u32 material, f32* out, u32 capacity) noexcept {
    if (!out || capacity < 4) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    InlineEditScope inlineScope(*this, layerId);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D) return false;
    const auto found = models_.find(l->model.scene.pack());
    if (found == models_.end() || !found->second || found->second->materials.empty()) return false;
    const auto& materials = found->second->materials;
    // "Modelo inteiro": o 1º material (o mesmo que o painel mostra nas réguas).
    const u32 m = material == kAllMaterials ? 0u : material;
    if (m >= materials.size()) return false;
    const FrameIndex local = l->local_time(editing_frame_locked());
    for (u32 p = 6; p < kMaterialParamCount; ++p) out[p - 6] = material_value(*l, materials[m], m, p, local);
    return true;
}


// -----------------------------------------------------------------------------
// Material
// -----------------------------------------------------------------------------

bool Engine::apply_model_material_preset(u64 layerId, u32 preset) noexcept {
    if (preset >= kModelMaterialPresets) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D || l->locked) return false;
    const auto found = models_.find(l->model.scene.pack());
    if (found == models_.end() || !found->second || found->second->materials.empty()) return false;
    const scene3d::SceneAsset& asset = *found->second;
    const u32 count = std::min<u32>(static_cast<u32>(asset.materials.size()), 4096u);
    history_.before_mutation(*comp, project_->timeline().current(), "visual do modelo 3D");
    // Todo visual parte do arquivo: o anterior não vaza (Cromo → Plástico não
    // fica prateado). As texturas trocadas (MaterialOverride::texture*) ficam.
    for (MaterialOverride& o : l->model.materials) o.mask = 0;
    if (preset == 0) {
        // Original: o arquivo de volta, inclusive sem os keyframes de material
        // (um passo de desfazer devolve tudo).
        l->tracks.remove_if([](const Track& t) { return t.property == TrackProperty::MaterialParam; });
    } else {
        const ModelLook& look = kLooks[preset];
        const FrameIndex local = l->local_time(editing_frame_locked());
        for (u32 i = 0; i < count; ++i)
            for (u32 p = 0; p < kMaterialParamCount; ++p)
                if (look.mask & (1u << p)) write_material(*l, asset.materials[i], i, p, look.v[p], local);
    }
    auto& overs = l->model.materials;
    overs.erase(std::remove_if(overs.begin(), overs.end(), [](const MaterialOverride& o) { return o.mask == 0 && !o.has_texture(); }),
                overs.end());
    bump_model_revision_locked();
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

i32 Engine::model_material_preset(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    InlineEditScope inlineScope(*this, layerId);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D) return -1;
    const auto found = models_.find(l->model.scene.pack());
    if (found == models_.end() || !found->second || found->second->materials.empty()) return -1;
    const u32 count = std::min<u32>(static_cast<u32>(found->second->materials.size()), 4096u);
    for (u32 preset = 0; preset < kModelMaterialPresets; ++preset) {
        const ModelLook& look = kLooks[preset];
        bool match = true;
        for (u32 i = 0; i < count && match; ++i) {
            const MaterialOverride* o = find_override(*l, i);
            const u32 mask = o ? o->mask : 0u;
            if (mask != look.mask) { match = false; break; }
            for (u32 p = 0; p < kMaterialParamCount && match; ++p)
                if ((look.mask & (1u << p)) && std::fabs(material_override_field(*o, p) - look.v[p]) > 1e-3f) match = false;
        }
        // Original também quer dizer "sem keyframe de material".
        if (match && preset == 0)
            for (u32 t = 0; t < l->tracks.size(); ++t)
                if (l->tracks.at(t).property == TrackProperty::MaterialParam && l->tracks.at(t).driven()) match = false;
        if (match) return static_cast<i32>(preset);
    }
    return -1;
}

bool Engine::model_material_preset_preview(u64 layerId, u32 preset, u32 size, std::vector<u8>& out) noexcept {
    out.clear();
    if (preset >= kModelMaterialPresets) return false;
    if (size < scene3d::kMaterialPreviewMinSize || size > scene3d::kMaterialPreviewMaxSize) return false;
    // O asset fica seguro pelo shared_ptr depois de soltar a trava (a bola lê
    // os pixels do mapa de cor fora dela), como em material_preview.
    std::shared_ptr<const scene3d::SceneAsset> asset;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        InlineEditScope inlineScope(*this, layerId);
        const Composition* comp = project_ ? current_composition() : nullptr;
        const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
        if (!l || l->kind != LayerKind::Model3D) return false;
        const auto found = models_.find(l->model.scene.pack());
        if (found == models_.end() || !found->second || found->second->materials.empty()) return false;
        asset = found->second;
    }
    scene3d::MaterialBall ball = scene3d::material_ball_of(*asset, asset->materials.front());
    const ModelLook& look = kLooks[preset];
    f32 fields[6]{ball.baseColor.x, ball.baseColor.y, ball.baseColor.z, ball.baseColor.w, ball.metallic, ball.roughness};
    for (u32 p = 0; p < 6; ++p) if (look.mask & (1u << p)) fields[p] = look.v[p];
    ball.baseColor = Vec4{fields[0], fields[1], fields[2], fields[3]};
    ball.metallic = fields[4];
    ball.roughness = fields[5];
    // Emissão do visual (v76, Neon): a bola recebe a luz própria já com a força.
    if (look.mask & kGlow) {
        const scene3d::Material& first = asset->materials.front();
        Vec3 e = first.emissive;
        f32 strength = first.emissiveStrength;
        if (look.mask & (1u << 6)) e.x = look.v[6];
        if (look.mask & (1u << 7)) e.y = look.v[7];
        if (look.mask & (1u << 8)) e.z = look.v[8];
        if (look.mask & (1u << 9)) strength = look.v[9];
        ball.emissive = e * std::max(0.0f, strength);
    }
    // As mesmas regras do renderer: opacidade abaixo de 1 mistura; metal ou
    // rugosidade ajustados ligam a luz de um material unlit.
    if ((look.mask & kAlpha) && ball.alphaMode == scene3d::AlphaMode::Opaque && ball.baseColor.w < 1.0f)
        ball.alphaMode = scene3d::AlphaMode::Blend;
    if (look.mask & (kMetal | kRough)) ball.unlit = false;
    out = scene3d::material_ball_cached(ball, size);
    return !out.empty();
}

bool Engine::toggle_material_keys(u64 layerId, u32 material, u32 paramMask) noexcept {
    paramMask &= kMaterialParamMask;
    if (!paramMask) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D || l->locked) return false;
    const auto found = models_.find(l->model.scene.pack());
    if (found == models_.end() || !found->second || found->second->materials.empty()) return false;
    const scene3d::SceneAsset& asset = *found->second;
    const u32 count = std::min<u32>(static_cast<u32>(asset.materials.size()), 4096u);
    if (material != kAllMaterials && material >= count) return false;
    const u32 first = material == kAllMaterials ? 0u : material;
    const u32 last = material == kAllMaterials ? count : material + 1u;
    const FrameIndex local = l->local_time(editing_frame_locked());
    bool here = false;
    for (u32 i = first; i < last && !here; ++i)
        for (u32 p = 0; p < kMaterialParamCount; ++p) {
            if (!(paramMask & (1u << p))) continue;
            const Track* t = l->tracks.find(TrackProperty::MaterialParam, i, p);
            if (t && t->has_expression()) return false;
            if (t && t->find_exact(local) != kInvalidIndex) { here = true; break; }
        }
    history_.before_mutation(*comp, project_->timeline().current(), here ? "tirar keyframe de material" : "keyframe de material");
    for (u32 i = first; i < last; ++i)
        for (u32 p = 0; p < kMaterialParamCount; ++p) {
            if (!(paramMask & (1u << p))) continue;
            if (here) {
                Track* t = l->tracks.find(TrackProperty::MaterialParam, i, p);
                if (!t || t->find_exact(local) == kInvalidIndex) continue;
                // O último keyframe vira o ajuste parado: o visual não pula.
                if (t->keys.size() == 1) {
                    MaterialOverride& o = ensure_override(*l, i, asset.materials[i]);
                    o.mask |= 1u << p;
                    set_material_override_field(o, p, std::clamp(t->keys.front().value, 0.0f, material_param_max(p)));

                }
                (void)t->remove(local);
            } else {
                const f32 v = material_value(*l, asset.materials[i], i, p, local);
                Track& t = l->tracks.get_or_create(TrackProperty::MaterialParam, i, p);
                (void)t.set(local, v, Interpolation::Linear);
            }
        }
    bump_model_revision_locked();
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

} // namespace aurea
