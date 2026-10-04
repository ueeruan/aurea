// =============================================================================
//  Aurea / engine / EngineMedia.cpp
//
//  "Substituir mídia", o caminho do arquivo de origem para "Informações da
//  mídia" e a lista de mídias de um projeto fechado (arquivo do projeto).
//
//  Substituir troca SÓ a fonte: a camada continua a mesma (id, transform,
//  keyframes, efeitos, máscaras, tempo, pai, linha). A mídia nova cabe na
//  caixa da antiga — a pessoa troca o clipe e o enquadramento continua —, e
//  o que mora no espaço da camada (âncora, máscaras) anda na proporção.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
std::shared_ptr<const CubeLut> Engine::cube_lookup(void* context, AssetId id) {
    auto& engine = *static_cast<Engine*>(context);
    if (auto it = engine.cubeLuts_.find(id.pack()); it != engine.cubeLuts_.end()) return it->second;
    const Asset* asset = engine.project_ ? engine.project_->asset(id) : nullptr;
    if (!asset || asset->kind != AssetKind::Lut) return {};
    auto parsed = read_cube_lut(engine.resolve_asset_path(asset->sourcePath));
    auto data = parsed.ok() ? std::make_shared<const CubeLut>(std::move(*parsed)) : nullptr;
    if (engine.cubeLuts_.size() >= 8) engine.cubeLuts_.clear();
    engine.cubeLuts_[id.pack()] = data;
    return data;
}

Status Engine::import_color_lut(u64 layerId, u32 effectId, const char* path) noexcept {
    if (!path || !*path) return Errc::InvalidArgument;
    auto parsed = read_cube_lut(path);
    if (!parsed.ok()) return parsed.status();
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || layer->locked) return Errc::InvalidState;
    auto effect = std::find_if(layer->effects.begin(), layer->effects.end(), [&](const EffectInstance& e) {
        return e.id == effectId && e.type == effect_type_id(effect_keys::kCubeLut);
    });
    if (effect == layer->effects.end() || effect->params.size() < 6) return Errc::NotFound;
    history_.before_mutation(*comp, project_->timeline().current(), "importar LUT");
    Asset asset; asset.kind = AssetKind::Lut;
    asset.sourcePath = store_asset_path(path);
    asset.name = path;
    if (const usize slash = asset.name.find_last_of("/\\"); slash != std::string::npos) asset.name.erase(0, slash + 1);
    const AssetId id = project_->add_asset(std::move(asset));
    effect->params[0].constant.ref = id.pack();
    effect->params[2].constant = ParamValue::scalar(static_cast<f32>(parsed->size));
    effect->params[3].constant = ParamValue::scalar(static_cast<f32>(parsed->dimensions));
    const Vec3 lo = parsed->domainMin, hi = parsed->domainMax;
    effect->params[4].constant = ParamValue::vec3(lo.x, lo.y, lo.z);
    effect->params[5].constant = ParamValue::vec3(hi.x, hi.y, hi.z);
    if (cubeLuts_.size() >= 8) cubeLuts_.clear();
    cubeLuts_[id.pack()] = std::make_shared<const CubeLut>(std::move(*parsed));
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty(); request_render(); return OkStatus;
}

std::string Engine::color_lut_name(u64 layerId, u32 effectId) const noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? project_->timeline().composition(project_->timeline().current()) : nullptr;
    const Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer) return {};
    for (const auto& e : layer->effects) if (e.id == effectId && e.type == effect_type_id(effect_keys::kCubeLut) && !e.params.empty()) {
        const Asset* asset = project_->asset(AssetId::unpack(e.params[0].constant.ref));
        return asset ? asset->name : std::string{};
    }
    return {};
}

namespace {

void scale_track(TrackSet& tracks, TrackProperty p, f32 k) {
    Track* t = tracks.find(p);
    if (!t) return;
    t->staticValue *= k;
    for (Keyframe& key : t->keys) {
        key.value *= k;
        key.tangentIn *= k;
        key.tangentOut *= k;
    }
}

void scale_points(std::vector<MaskPoint>& pts, f32 rx, f32 ry) {
    for (MaskPoint& p : pts) {
        p.position.x *= rx; p.position.y *= ry;
        p.inTangent.x *= rx; p.inTangent.y *= ry;
        p.outTangent.x *= rx; p.outTangent.y *= ry;
    }
}

/// Espaço da camada de `oldW×oldH` para `newW×newH`: o que é posição na mídia
/// (âncora, máscaras) anda na proporção; a escala compensa para a mídia nova
/// caber na caixa da antiga (nem maior, nem esticada).
void remap_layer_space(Layer& l, f32 oldW, f32 oldH, f32 newW, f32 newH) {
    if (oldW <= 0.0f || oldH <= 0.0f || newW <= 0.0f || newH <= 0.0f) return;
    const f32 rx = newW / oldW, ry = newH / oldH;
    const f32 k = std::min(oldW / newW, oldH / newH);
    l.transform.anchor.x *= rx;
    l.transform.anchor.y *= ry;
    l.transform.scale.x *= k;
    l.transform.scale.y *= k;
    scale_track(l.tracks, TrackProperty::AnchorX, rx);
    scale_track(l.tracks, TrackProperty::AnchorY, ry);
    scale_track(l.tracks, TrackProperty::ScaleX, k);
    scale_track(l.tracks, TrackProperty::ScaleY, k);
    for (Mask& m : l.masks) {
        scale_points(m.points, rx, ry);
        for (MaskPathKey& key : m.pathKeys) scale_points(key.points, rx, ry);
        m.cacheKey = 0;
    }
}

/// Vídeo mais curto que o trecho: o fim encolhe até onde a fonte chega. A
/// entrada (`offset`) fica, a menos que já passe do fim da fonte nova.
void clamp_to_source(Layer& l, i64 sourceFrames) {
    if (sourceFrames <= 0 || l.timeRemapEnabled) return;
    if (l.offset.value >= sourceFrames) l.offset = FrameIndex{0};
    const f64 speed = std::abs(static_cast<f64>(l.speed));
    if (speed < 1e-6) return;   // quadro congelado: qualquer duração serve
    const i64 fits = static_cast<i64>(std::floor(static_cast<f64>(sourceFrames - l.offset.value) / speed + 1e-6));
    const i64 span = l.end.value - l.start.value;
    if (fits >= span) return;
    l.end = FrameIndex{l.start.value + std::max<i64>(1, fits)};
    l.fadeIn = FrameIndex{std::min(l.fadeIn.value, l.end.value - l.start.value)};
    l.fadeOut = FrameIndex{std::min(l.fadeOut.value, l.end.value - l.start.value)};
}

/// Tamanho da mídia que a camada mostra agora (vídeo: o asset; imagem: pixels).
bool current_media_size(const Project& project, const std::unordered_map<u64, ImagePixels>& images,
                        const Layer& l, f32& w, f32& h) {
    if (const Asset* a = project.asset(l.source); a && a->video.width > 0 && a->video.height > 0) {
        w = static_cast<f32>(a->video.width);
        h = static_cast<f32>(a->video.height);
        return true;
    }
    if (const auto it = images.find(l.source.pack()); it != images.end() && it->second.width > 0) {
        w = static_cast<f32>(it->second.width);
        h = static_cast<f32>(it->second.height);
        return true;
    }
    return false;
}

} // namespace

Result<u64> Engine::replace_layer_video(u64 layerId, const VideoImport& request) noexcept {
    VideoSourceFactory* factory = config_.mediaFactory;
    if (!factory) return Status{Errc::NotSupported, "sem decodificador de video nesta plataforma"};
    MediaProbe probe;
    if (!factory->probe(request.sourcePath.c_str(), probe) || !probe.hasVideo) {
        return Status{Errc::UnsupportedFormat, "arquivo sem trilha de video decodificavel"};
    }
    const VideoStreamInfo& v = probe.video;
    const u32 dispW = v.display_width();
    const u32 dispH = v.display_height();
    if (dispW == 0 || dispH == 0) return Status{Errc::AssetCorrupted, "video sem dimensoes"};

    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    const LayerId id = LayerId::unpack(layerId);
    {
        const Layer* l = comp->layer(id);
        if (!l || (l->kind != LayerKind::Video && l->kind != LayerKind::Image)) {
            return Status{Errc::NotFound, "camada de video ou imagem nao encontrada"};
        }
    }
    history_.before_mutation(*comp, project_->timeline().current(), "substituir midia");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);

    Asset asset;
    asset.kind = AssetKind::Video;
    asset.name = request.displayName.empty() ? std::string("Video") : request.displayName;
    asset.sourcePath = request.sourcePath;
    asset.originalFilename = request.displayName;
    asset.video.width = dispW;
    asset.video.height = dispH;
    asset.video.fps = v.fps > 0.0 ? v.fps : 30.0;
    asset.timebaseFps = asset.video.fps;
    asset.video.frameCount = FrameIndex{static_cast<i64>(std::llround(static_cast<f64>(v.durationUs) * asset.video.fps / 1e6))};
    asset.duration = asset.video.frameCount;
    asset.profile.bitDepth = v.color.bitDepth;
    asset.profile.hdr = v.color.hdr();
    asset.profile.transfer = v.color.transfer == TransferFunction::PQ ? ColorSpace::HDR10
                           : v.color.transfer == TransferFunction::HLG ? ColorSpace::HLG : ColorSpace::Rec709;
    asset.profile.primaries = v.color.primaries == ColorPrimaries::BT2020 ? ColorSpace::Rec2020
                            : v.color.primaries == ColorPrimaries::P3 ? ColorSpace::DisplayP3 : ColorSpace::Rec709;
    if (probe.hasAudio) {
        asset.audio.sampleRate = probe.audioSampleRate;
        asset.audio.channels = probe.audioChannels;
        asset.audio.sampleCount = FrameIndex{probe.audioDurationUs * static_cast<i64>(probe.audioSampleRate) / 1'000'000};
    } else {
        asset.audio.sampleRate = 0;
        asset.audio.channels = 0;
    }

    Layer* l = comp->layer(id);
    f32 oldW = 0.0f, oldH = 0.0f;
    const bool hadSize = current_media_size(*project_, images_, *l, oldW, oldH);
    const AssetId assetId = project_->add_asset(std::move(asset));
    l = comp->layer(id);   // add_asset não mexe nas camadas; relido por garantia
    if (!l) return Status{Errc::InvalidState, "camada sumiu"};
    l->kind = LayerKind::Video;
    l->source = assetId;
    if (hadSize) remap_layer_space(*l, oldW, oldH, static_cast<f32>(dispW), static_cast<f32>(dispH));
    const i64 sourceFrames = static_cast<i64>(std::ceil(static_cast<f64>(v.durationUs) * comp->fps() / 1e6 - 1e-6));
    clamp_to_source(*l, sourceFrames);
    l->cacheKey = 0;

    project_->mark_dirty();
    request_render();
    AUREA_LOG_INFO("midia substituida: video %ux%u %.3f fps", dispW, dispH, v.fps);
    return id.pack();
}

Result<u64> Engine::replace_layer_image(u64 layerId, const u8* rgba, u32 width, u32 height,
                                        const char* name, const char* sourcePath) noexcept {
    if (!rgba || width == 0 || height == 0) return Status{Errc::InvalidArgument, "imagem vazia"};
    if (width > 65536 || height > 65536) return Status{Errc::BudgetExceeded, "imagem grande demais"};
    const u64 bytes = static_cast<u64>(width) * height * 4;
    std::lock_guard<std::recursive_mutex> importing(sourceImportMutex_);
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    const LayerId id = LayerId::unpack(layerId);
    Layer* l = comp->layer(id);
    if (!l || (l->kind != LayerKind::Video && l->kind != LayerKind::Image)) {
        return Status{Errc::NotFound, "camada de video ou imagem nao encontrada"};
    }
    if (bytes + sizeof(ImagePixels) > source_asset_room_locked())
        return Status{Errc::BudgetExceeded, "memoria de imagens e modelos do projeto esgotada"};
    ImagePixels px; px.width = width; px.height = height;
    try { px.rgba.assign(rgba, rgba + static_cast<usize>(bytes)); }
    catch (const std::bad_alloc&) { return Status{Errc::OutOfMemory}; }
    history_.before_mutation(*comp, project_->timeline().current(), "substituir midia");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);

    f32 oldW = 0.0f, oldH = 0.0f;
    const bool hadSize = current_media_size(*project_, images_, *l, oldW, oldH);
    Asset asset;
    asset.kind = AssetKind::Image;
    asset.name = name && *name ? name : "Imagem";
    asset.sourcePath = sourcePath ? sourcePath : "";
    asset.originalFilename = asset.name;
    asset.video.width = width;
    asset.video.height = height;
    const AssetId assetId = project_->add_asset(std::move(asset));

    images_[assetId.pack()] = std::move(px);

    l = comp->layer(id);
    if (!l) return Status{Errc::InvalidState, "camada sumiu"};
    l->kind = LayerKind::Image;
    l->source = assetId;
    if (hadSize) remap_layer_space(*l, oldW, oldH, static_cast<f32>(width), static_cast<f32>(height));
    l->cacheKey = 0;
    project_->mark_dirty();
    request_render();
    return id.pack();
}

std::string Engine::layer_source_path(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || (l->kind != LayerKind::Video && l->kind != LayerKind::Audio && l->kind != LayerKind::Image)) return {};
    const Asset* a = project_->asset(l->source);
    return a && !a->sourcePath.empty() ? resolve_asset_path(a->sourcePath) : std::string{};
}

Status Engine::project_file_media(const char* path, std::vector<package::MediaRef>& out) noexcept {
    if (!path || !*path) return Errc::InvalidArgument;
    const Status s = package::list_media(path, out);
    if (!s.ok()) return s;
    for (package::MediaRef& m : out) {
        const std::string r = resolve_asset_path(m.stored);
        m.resolved = r.empty() ? m.stored : r;
    }
    return OkStatus;
}

} // namespace aurea
