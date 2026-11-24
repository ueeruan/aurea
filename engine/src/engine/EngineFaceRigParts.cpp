// =============================================================================
//  Aurea / engine / EngineFaceRigParts.cpp
//
//  Rig 2D v2 — "Separar em partes" (effects/FaceRigParts.hpp). A camada de
//  imagem com o aurea.rig.face vira uma pré-composição "Rig — <camada>":
//
//   - uma camada de imagem por parte, de trás para a frente (cabelo de trás,
//     corpo, pescoço, rosto, olhos, sobrancelhas, nariz, boca, franja, mechas,
//     óculos), cada uma com o aurea.rig.part ligado ao Controle;
//   - o nulo "Controle do rig" em cima, com uma cópia do aurea.rig.face (os
//     mesmos 12 parâmetros e keyframes, as âncoras e a "Física do cabelo").
//
//  Os pixels de cada parte vão para PNGs em Documents/Media (pasta derivada,
//  como a expansão do PSD): o projeto guarda o caminho e reabre igual. A
//  separação (CPU, pesada) roda fora do lock do modelo; o resto é UM passo de
//  desfazer na composição atual. A camada original fica oculta, não apagada.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/FaceRigParts.hpp"
#include "aurea/export/ImageEncode.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/project/MediaStorage.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace aurea {
namespace {

using namespace face_rig;

std::atomic<f32> gSplitProgress{1.0f};

void on_split_progress(void*, f32 done) noexcept {
    gSplitProgress.store(0.02f + 0.86f * saturate(done), std::memory_order_relaxed);
}

Vec2 param2(const EffectInstance& e, u32 p) noexcept { return Vec2{e.params[p].constant.v[0], e.params[p].constant.v[1]}; }

Anchors anchors_of(const EffectInstance& e) noexcept {
    Anchors a;
    a.face = param2(e, kFaceCenter);
    a.faceR = param2(e, kFaceSize);
    a.eye[0] = param2(e, kEyeLeft);
    a.eyeR[0] = param2(e, kEyeLeftSize);
    a.eye[1] = param2(e, kEyeRight);
    a.eyeR[1] = param2(e, kEyeRightSize);
    a.mouth = param2(e, kMouth);
    a.mouthR = param2(e, kMouthSize);
    a.neck = param2(e, kNeck);
    a.hairTop = param2(e, kHairTop);
    return a;
}

/// Ponto da camada (px) no espaço do PAI dela (a transformação local 2D).
Vec2 local_point(const Transform& t, Vec2 p) noexcept {
    const f32 sx = (p.x - t.anchor.x) * t.scale.x, sy = (p.y - t.anchor.y) * t.scale.y;
    const f32 a = t.rotation.z * kDeg2Rad, c = std::cos(a), s = std::sin(a);
    return Vec2{t.position.x + c * sx - s * sy, t.position.y + s * sx + c * sy};
}

} // namespace

f32 Engine::face_rig_split_progress() const noexcept { return gSplitProgress.load(std::memory_order_relaxed); }

Result<u64> Engine::face_rig_split_parts(u64 layerId, u32 effectId, u64* controllerOut) noexcept try {
    if (controllerOut) *controllerOut = 0;
    std::lock_guard<std::recursive_mutex> admission(sourceImportMutex_);
    struct ProgressDone {
        ~ProgressDone() { gSplitProgress.store(1.0f, std::memory_order_relaxed); }
    } progressDone;
    gSplitProgress.store(0.01f, std::memory_order_relaxed);

    // 1) Sob o lock: a imagem, as âncoras, o efeito e onde a camada está.
    std::shared_ptr<const ImagePixels> pixels;
    EffectInstance rigEffect;
    std::vector<Track> rigTracks, transformTracks;
    Transform transform;
    FrameIndex start{0}, end{0}, offset{0};
    LayerId parent{};
    std::string name;
    u64 session = 0, room = 0;
    CompositionId compId{};
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
        drain_commands_locked();
        Composition* comp = current_composition();
        Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
        if (!l || l->kind != LayerKind::Image) return Status{Errc::InvalidArgument, "precisa de uma camada de imagem"};
        if (l->locked) return Status{Errc::InvalidState, "camada travada"};
        const EffectInstance* e = nullptr;
        for (const EffectInstance& item : l->effects)
            if (item.id == effectId && item.type == effect_type_id(kFaceRigKey) && item.params.size() >= kParamCount) e = &item;
        if (!e) return Status{Errc::NotFound, "camada sem o Rig 2D de rosto"};
        const auto it = images_.find(l->source.pack());
        if (it == images_.end() || !it->second || !it->second->width || !it->second->height
            || it->second->rgba.size() < static_cast<usize>(it->second->width) * it->second->height * 4)
            return Status{Errc::NotFound, "imagem da camada indisponível"};
        pixels = it->second;
        rigEffect = *e;
        for (u32 i = 0; i < l->tracks.size(); ++i) {
            const Track& t = l->tracks.at(i);
            if (t.property == TrackProperty::EffectParam) {
                if (t.effectIndex == e->id) rigTracks.push_back(t);
            } else if (static_cast<u16>(t.property) <= static_cast<u16>(TrackProperty::SkewY)) {
                transformTracks.push_back(t);
            }
        }
        transform = l->transform;
        start = l->start;
        end = l->end;
        offset = l->offset;
        parent = l->parent;
        name = l->name.empty() ? std::string("Imagem") : l->name;
        session = projectSession_;
        compId = project_->timeline().current();
        room = source_asset_room_locked();
        if (project_->timeline().composition_count() + 1 > 256 || comp->nesting_depth() + 2 > kMaxNestingDepth)
            return Status{Errc::BudgetExceeded, "composições demais"};
    }

    // 2) Fora do lock: separar e preencher (CPU).
    const ImagePixels& px = *pixels;
    PartSet set;
    if (!split_parts(px.rgba.data(), px.width, px.height, anchors_of(rigEffect), set, &on_split_progress, nullptr))
        return Status{Errc::InvalidArgument, "nada para separar"};
    u64 bytes = 0;
    for (const Part& p : set.parts) bytes += static_cast<u64>(p.w) * p.h * 4;
    if (bytes > room / 2) return Status{Errc::BudgetExceeded, "memoria de imagens do projeto esgotada"};

    // 3) PNG de cada parte numa pasta derivada (a chave é o conteúdo + as âncoras).
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path base = config_.documentsDirectory.empty() ? fs::temp_directory_path(error) : fs::u8path(config_.documentsDirectory) / "Media";
    if (error) return Status{Errc::IoError, "pasta de mídia indisponível"};
    u64 key = storage::content_hash(px.rgba.data(), px.rgba.size());
    {
        const Anchors a = anchors_of(rigEffect);
        key ^= storage::content_hash(reinterpret_cast<const u8*>(&a), sizeof a) * 1099511628211ull;
    }
    char folderName[64];
    std::snprintf(folderName, sizeof folderName, "RIG-%016llx-%ux%u", static_cast<unsigned long long>(key), px.width, px.height);
    const fs::path folder = base / folderName;
    if (!fs::exists(folder, error) && (!fs::create_directories(folder, error) || error))
        return Status{Errc::IoError, "pasta das partes não criada"};
    std::vector<std::string> sources;
    sources.reserve(set.parts.size());
    std::vector<u8> png;
    for (const Part& p : set.parts) {
        char file[32];
        std::snprintf(file, sizeof file, "%02u.png", p.role);
        const auto utf = (folder / file).u8string();
        sources.emplace_back(utf.begin(), utf.end());
        png.clear();
        if (!encode_png(p.rgba.data(), p.w, p.h, true, png).ok()) return Status{Errc::IoError, "png da parte"};
        if (!fileio::write_atomic(sources.back(), png.data(), png.size()).ok()) return Status{Errc::IoError, "png da parte"};
    }
    {
        const auto utf = folder.u8string();
        (void)storage::mark_derived(std::string(utf.begin(), utf.end()));
    }
    gSplitProgress.store(0.95f, std::memory_order_relaxed);

    // 4) Sob o lock: a pré-composição, as camadas e o Controle — um passo de desfazer.
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_ || session != projectSession_ || compId != project_->timeline().current()) return Status{Errc::Cancelled};
    drain_commands_locked();
    auto& timeline = project_->timeline();
    {
        const Composition* comp = timeline.composition(compId);
        const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
        if (!l || l->locked || l->kind != LayerKind::Image) return Status{Errc::Cancelled};
        if (comp->layers().count() + 1 >= kMaxLayerCount) return Status{Errc::BudgetExceeded};
    }
    const ParameterRegistry* partParams = effectRegistry_.params(effect_type_id(kFaceRigPartKey));
    if (!partParams) return Status{Errc::NotSupported};
    const std::string title = std::string("Rig \xE2\x80\x94 ") + name;
    u32 compW = 0, compH = 0;
    f64 fps = 30.0;
    FrameIndex duration{0};
    u32 depth = 0;
    {
        const Composition* comp = timeline.composition(compId);
        compW = comp->width();
        compH = comp->height();
        fps = comp->fps();
        duration = comp->duration();
        depth = comp->nesting_depth();
    }
    const CompositionId innerId = timeline.create_composition(title, compW, compH, fps);
    if (!innerId.valid()) return Status{Errc::OutOfMemory};
    // Criar composição pode mover a tabela: os ponteiros vêm depois.
    Composition* inner = timeline.composition(innerId);
    inner->set_duration(duration);
    inner->set_nesting_depth(depth + 1);
    inner->set_transparent_background(true);

    // O Controle fica no centro do rosto (a alça dele no palco cai ali); as
    // partes ficam no mesmo lugar da imagem (posição compensada pelo pai).
    const Rig rig = rig_of(anchors_of(rigEffect), static_cast<f32>(px.width), static_cast<f32>(px.height));
    const Vec2 center = local_point(transform, rig.face);
    const Vec3 nullAnchor{50.0f, 50.0f, 0.0f};
    const Vec2 shift{nullAnchor.x - center.x, nullAnchor.y - center.y};

    const LayerId nullId = inner->add_layer(LayerKind::Null, "Controle do rig");
    Layer* nl = inner->layer(nullId);
    if (!nl) return Status{Errc::OutOfMemory};
    nl->start = start;
    nl->end = end;
    nl->transform.anchor = nullAnchor;
    nl->transform.position = Vec3{center.x, center.y, 0.0f};
    {
        EffectInstance control = rigEffect;
        control.id = nl->alloc_effect_id();
        control.params[kPartsRig].constant.v[0] = 1.0f;
        control.params[kPartsRig].source = ParamSource::Constant;
        // A boca do rig passa a ser a da parte separada: uma boca já aberta no
        // desenho abre pelo meio, na largura dela (não só na linha de cima).
        for (const Part& p : set.parts) {
            if (p.role != kPartMouth || p.w < 2 || p.h < 2) continue;
            const f32 fw = static_cast<f32>(px.width), fh = static_cast<f32>(px.height);
            const f32 cx = static_cast<f32>(p.x0) + 0.5f * static_cast<f32>(p.w), cy = static_cast<f32>(p.y0) + 0.5f * static_cast<f32>(p.h);
            control.params[kMouth].constant = ParamValue::vec2(cx / fw, cy / fh);
            control.params[kMouthSize].constant = ParamValue::vec2(0.5f * static_cast<f32>(p.w) / fw,
                                                                   std::max(0.12f * static_cast<f32>(p.h), 1.0f) / fh);
        }
        for (Track t : rigTracks) {
            t.effectIndex = control.id;
            nl->tracks.add(std::move(t));
        }
        nl->effects.push_back(std::move(control));
    }

    std::vector<LayerId> partIds;
    for (usize k = 0; k < set.parts.size(); ++k) {
        Part& p = set.parts[k];
        Asset asset;
        asset.kind = AssetKind::Image;
        asset.name = part_name(p.role);
        asset.originalFilename = asset.name + ".png";
        asset.sourcePath = store_asset_path(sources[k]);
        asset.video.width = p.w;
        asset.video.height = p.h;
        const AssetId assetId = project_->add_asset(std::move(asset));
        ImagePixels partPx;
        partPx.width = p.w;
        partPx.height = p.h;
        partPx.rgba = std::move(p.rgba);
        images_[assetId.pack()] = std::make_shared<const ImagePixels>(std::move(partPx));

        const LayerId lid = inner->add_layer(LayerKind::Image, part_name(p.role));
        Layer* pl = inner->layer(lid);
        if (!pl) return Status{Errc::OutOfMemory};
        pl->source = assetId;
        pl->start = start;
        pl->end = end;
        pl->offset = offset;
        pl->transform = transform;
        // O recorte começa em (x0, y0): o pivô anda junto e a parte fica no lugar.
        pl->transform.anchor = Vec3{transform.anchor.x - static_cast<f32>(p.x0), transform.anchor.y - static_cast<f32>(p.y0), transform.anchor.z};
        pl->transform.position = Vec3{transform.position.x + shift.x, transform.position.y + shift.y, transform.position.z};
        pl->parent = nullId;
        for (Track t : transformTracks) {
            const f32 dv = t.property == TrackProperty::PositionX ? shift.x
                         : t.property == TrackProperty::PositionY ? shift.y
                         : t.property == TrackProperty::AnchorX ? -static_cast<f32>(p.x0)
                         : t.property == TrackProperty::AnchorY ? -static_cast<f32>(p.y0) : 0.0f;
            if (dv != 0.0f) {
                t.staticValue += dv;
                for (Keyframe& key2 : t.keys) key2.value += dv;
            }
            pl->tracks.add(std::move(t));
        }
        EffectInstance pe;
        pe.id = pl->alloc_effect_id();
        pe.type = effect_type_id(kFaceRigPartKey);
        initialize_instance(pe, *partParams);
        pe.params[kPartParamRole].constant.v[0] = static_cast<f32>(p.role);
        pe.params[kPartParamDepth].constant.v[0] = part_default_depth(p.role);
        pe.params[kPartParamController].constant.ref = nullId.pack();
        pe.params[kPartParamOrigin].constant = ParamValue::vec2(static_cast<f32>(p.x0), static_cast<f32>(p.y0));
        pe.params[kPartParamFullSize].constant = ParamValue::vec2(static_cast<f32>(px.width), static_cast<f32>(px.height));
        const bool leftEye = p.role == kPartEyeWhiteL || p.role == kPartIrisL || p.role == kPartLashL;
        const bool rightEye = p.role == kPartEyeWhiteR || p.role == kPartIrisR || p.role == kPartLashR;
        if (leftEye || rightEye) pe.params[kPartParamLid].constant = ParamValue::vec2(set.lid[rightEye ? 1 : 0].x, set.lid[rightEye ? 1 : 0].y);
        pl->effects.push_back(std::move(pe));
        partIds.push_back(lid);
    }
    // O Controle no topo da pilha (nulo não desenha; fica fácil de achar).
    (void)inner->reorder_layer(nullId, static_cast<u32>(inner->order().size() - 1));
    inner->rebuild_draw_order();

    Composition* comp = timeline.composition(compId);
    if (!comp) return Status{Errc::Cancelled};
    history_.before_mutation(*comp, compId, "separar em partes");
    bump_model_revision_locked();
    const LayerId rigId = comp->add_layer(LayerKind::Composition, title);
    Layer* rl = comp->layer(rigId);
    if (!rl) return Status{Errc::OutOfMemory};
    rl->nested.composition = innerId;
    rl->start = FrameIndex{0};
    rl->end = duration.value > 0 ? duration : FrameIndex{1};
    rl->transform.anchor = Vec3{static_cast<f32>(compW) * 0.5f, static_cast<f32>(compH) * 0.5f, 0.0f};
    rl->transform.position = rl->transform.anchor;
    rl->parent = parent;
    Layer* original = comp->layer(LayerId::unpack(layerId));
    if (original) {
        original->visible = false;
        const i32 at = comp->z_index_of(LayerId::unpack(layerId));
        if (at >= 0) (void)comp->reorder_layer(rigId, static_cast<u32>(at + 1));
    }
    comp->rebuild_draw_order();
    project_->mark_dirty();
    request_render();
    if (controllerOut) *controllerOut = nullId.pack();
    return rigId.pack();
} catch (const std::bad_alloc&) {
    return Status{Errc::OutOfMemory};
}

} // namespace aurea
