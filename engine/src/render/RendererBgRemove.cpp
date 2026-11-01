// =============================================================================
//  Aurea / render / RendererBgRemove.cpp
//
//  "Remover fundo" (aurea.key.background_remove): o recorte do quadro da fonte
//  que a camada mostra agora, do cache por quadro (ai::BgRemoveService).
//
//   - Preview: o que estiver no cache; o resto vai para o worker e o quadro
//     fica "incompleto" (volta quando o recorte ficar pronto). Enquanto isso,
//     um vizinho já pronto segura (melhor atrasado que piscando o original).
//   - Export/captura: espera o recorte EXATO deste quadro.
//   - Textura: a R16F 320² do Rotobrush ou, com a borda fina, o refinado na
//     resolução do quadro (R8/RGBA8): o shader do Rotobrush não muda.
// =============================================================================
#include "aurea/render/Renderer.hpp"

#include "aurea/ai/BackgroundRemoval.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/scene3d/Environment.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace {

u64 bmix(u64 h, u64 v) noexcept {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    return h ^ (h >> 29);
}

u64 bhash(const std::string& s) noexcept {
    u64 h = 0xCBF29CE484222325ull;
    for (const char c : s) { h ^= static_cast<u8>(c); h *= 0x100000001B3ull; }
    return h;
}

constexpr u32 kModelParam = 10;

/// O namespace dos mapas crus do U2Net: o MESMO do Rotobrush (um cache só).
u64 bg_raw_key(const Layer& l, const Asset& a) noexcept {
    return bmix(l.source.pack() ^ 0xDA561CD33ull, bhash(a.sourcePath) ^ a.contentHash);
}

u64 bg_key_of(const Asset& a, bool video, u32 model) noexcept {
    return video ? ai::bg_matte_key(a.contentHash, a.sourcePath, a.video.width, a.video.height,
                                    a.video.frameCount.value, model)
                 : ai::bg_matte_key(a.contentHash, a.sourcePath, 0, 0, 1, model);
}

u32 requested_model(const EffectInstance& instance) noexcept {
    if (instance.params.size() <= kModelParam) return ai::kBgModelAuto;
    return std::lround(instance.params[kModelParam].constant.v[0]) == 1 ? ai::kBgModelPerson : ai::kBgModelAuto;
}

/// Trecho da fonte que a camada usa ([first, last] em quadros da fonte).
struct ClipRange { i64 first = 0, last = 0, total = 1; f64 srcFps = 30.0; };

ClipRange clip_range(const Composition& comp, const Layer& layer, const Asset& asset) noexcept {
    ClipRange r;
    const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
    r.srcFps = asset.video.fps > 0.0 ? asset.video.fps : fps;
    r.total = asset.video.frameCount.value > 0 ? asset.video.frameCount.value : std::max<i64>(1, std::llround(r.srcFps * 10));
    auto source_index = [&](i64 timelineTime) {
        const f64 sf = layer.source_frame(FrameIndex{timelineTime});
        if (!std::isfinite(sf)) return i64{0};
        return std::clamp<i64>(static_cast<i64>(std::floor(sf / fps * r.srcFps + 1e-3)), 0, r.total - 1);
    };
    r.first = source_index(layer.start.value);
    r.last = source_index(std::max(layer.start.value, layer.end.value - 1));
    if (r.first > r.last) std::swap(r.first, r.last);
    return r;
}

} // namespace

void Renderer::set_bg_matte_store(std::shared_ptr<ai::BgMatteStore> store) noexcept {
    if (store == bgStore_) return;
    if (bg_) { bg_->clear(); bg_.reset(); }   // o serviço é preso ao store que recebeu
    bgStore_ = std::move(store);
}

void Renderer::bg_project_keys(const Project& project, std::vector<u64>& out) {
    out.clear();
    project.for_each_asset([&](AssetId, const Asset& a) {
        const bool video = a.kind == AssetKind::Video && a.has_video();
        if (!video && a.kind != AssetKind::Image) return;
        for (const u32 model : {ai::kBgModelAuto, ai::kBgModelPerson}) out.push_back(bg_key_of(a, video, model));
    });
}

bool Renderer::bg_analyze(const Project& project, const Composition& comp, const Layer& layer,
                          const EffectInstance& instance, MediaManager* media) noexcept try {
    if (instance.type != effect_type_id(effect_keys::kBackgroundRemove)) return false;
    if (layer.kind != LayerKind::Video || !media || !media->factory()) return layer.kind == LayerKind::Image;
    const Asset* asset = project.asset(layer.source);
    if (!asset || !asset->has_video()) return false;
    if (!foreground_) foreground_ = std::make_unique<ai::DepthMapService>(true);
    foreground_->retry_failed();   // pedido explícito: a rede que falhou tenta de novo
    if (!bgStore_) bgStore_ = ai::make_bg_matte_store();
    if (!bg_) {
        bg_ = std::make_shared<ai::BgRemoveService>(bgStore_);
        bg_->set_low_memory(lowMemoryDevice_);
    }
    bgStore_->set_max_bytes(lowMemoryDevice_ ? ai::BgMatteStore::kLowMemoryMaxBytes : ai::BgMatteStore::kMaxBytes);
    void (*wake)(void*) = nullptr; void* ctx = nullptr;
    media->ready_callback(wake, ctx);
    bg_->set_ready_callback(wake, ctx);
    const u32 model = ai::bg_effective_model(requested_model(instance));
    const ClipRange range = clip_range(comp, layer, *asset);
    ai::BgRemoveService::Source src;
    src.fg = foreground_.get();
    src.factory = media->factory();
    src.asset = *asset;
    src.rawKey = bg_raw_key(layer, *asset);
    src.matteKey = bg_key_of(*asset, true, model);
    src.model = model;
    src.fps = range.srcFps;
    src.frameCount = range.total;
    src.layerW = static_cast<f32>(asset->video.width);
    src.layerH = static_cast<f32>(asset->video.height);
    bg_->analyze(src, range.first, range.last);
    return true;
} catch (...) {
    return false;
}

bool Renderer::bg_status(const Project& project, const Composition& comp, const Layer& layer,
                         const EffectInstance& instance, i64 sourceFrame, i64* out8) noexcept try {
    if (!out8) return false;
    for (u32 i = 0; i < 8; ++i) out8[i] = 0;
    if (instance.type != effect_type_id(effect_keys::kBackgroundRemove)) return false;
    const Asset* asset = project.asset(layer.source);
    if (!asset) return false;
    const bool video = layer.kind == LayerKind::Video && asset->has_video();
    const u32 model = ai::bg_effective_model(requested_model(instance));
    out8[7] = model;
    if (!bgStore_) bgStore_ = ai::make_bg_matte_store();
    const u64 key = bg_key_of(*asset, video, model);
    i64 first = 0, last = 0;
    if (video) {
        const ClipRange range = clip_range(comp, layer, *asset);
        first = range.first;
        last = range.last;
    } else {
        sourceFrame = 0;
    }
    out8[6] = last - first + 1;
    out8[5] = bgStore_->count(key, first, last);
    out8[4] = bgStore_->has(key, std::max<i64>(0, sourceFrame)) ? 1 : 0;
    if (bg_) {
        const auto p = bg_->progress();
        if (p.matteKey == key) {
            out8[0] = p.done; out8[1] = p.total; out8[2] = p.running ? 1 : 0;
            out8[3] = p.failed ? std::max<i64>(1, static_cast<i64>(p.reason)) : 0;
        }
    }
    return true;
} catch (...) {
    return false;
}

DepthMapResult Renderer::bg_map(const DepthMapRequest& request) noexcept try {
    DepthMapResult none; none.failed = planFinal_;
    if (!backend_ || !request.host || !planProject_) return none;
    const Layer& l = *request.host;
    if (l.kind != LayerKind::Image && l.kind != LayerKind::Video) return none;
    const Asset* asset = planProject_->asset(l.source);
    if (!asset) return none;
    if (!foreground_) foreground_ = std::make_unique<ai::DepthMapService>(true);
    if (!bgStore_) bgStore_ = ai::make_bg_matte_store();
    if (!bg_) {
        bg_ = std::make_shared<ai::BgRemoveService>(bgStore_);
        bg_->set_low_memory(lowMemoryDevice_);
        bgStore_->set_max_bytes(lowMemoryDevice_ ? ai::BgMatteStore::kLowMemoryMaxBytes : ai::BgMatteStore::kMaxBytes);
    }
    if (planMedia_) {
        void (*wake)(void*) = nullptr; void* ctx = nullptr;
        planMedia_->ready_callback(wake, ctx);
        foreground_->set_ready_callback(wake, ctx);
        bg_->set_ready_callback(wake, ctx);
    }
    const bool wait = planFinal_ && !planDeferLocalAi_;
    const u32 model = ai::bg_effective_model(request.bgModel);
    const u64 rawKey = bg_raw_key(l, *asset);
    ai::RotoRefineSettings refineSettings;
    refineSettings.radius = std::isfinite(request.rotoEdgeRadius) ? std::max(0.0f, request.rotoEdgeRadius) : 0.0f;
    refineSettings.decontaminate = request.rotoDecontaminate;
    const bool refine = refineSettings.on();
    const u64 refineKey = refine ? refineSettings.hash() : 0;
    const u32 refineMaxSide = lowMemoryDevice_ ? 1280u : ai::kRotoRefineMaxSide;

    ai::BgRemoveService::MattePtr matte;
    ai::BgRemoveService::RefinedPtr refined;
    u64 matteKey = 0;
    i64 frame = 0, sourceTimeUs = -1;
    f64 srcFps = 30.0;
    if (l.kind == LayerKind::Image) {
        const ImagePixels* px = planImageLookup_ ? planImageLookup_(planImageCtx_, l.source) : nullptr;
        if (!px || !px->width || !px->height || px->rgba.size() < static_cast<usize>(px->width) * px->height * 4)
            return none;
        matteKey = bg_key_of(*asset, false, model);
        matte = bg_->cached(matteKey, 0);
        if (!matte) {
            // Uma foto: o recorte sai uma vez (a rede no worker do mapa; o
            // resto, rápido, aqui) e fica no cache/arquivo.
            ai::BgRawPtr raw;
            if (model == ai::kBgModelPerson) {
                ai::RotoImage small;
                ai::roto_image_from(px->rgba.data(), px->width, px->height, px->width * 4, 4, ai::kBgNativeSide, small);
                raw = ai::BgRemoveService::raw_from_native(model, small);
            }
            if (!raw) {
                const u64 frameKey = bmix(bmix(rawKey, 0x1D), (static_cast<u64>(px->width) << 32) | px->height);
                const ai::DepthMapPtr map = foreground_->image(frameKey, px->rgba.data(), px->width, px->height,
                                                               px->width * 4, 4, wait);
                if (!map || map->disparity.size() != static_cast<usize>(ai::kBgMatteSize) * ai::kBgMatteSize
                    || map->foregroundRgb.size() != map->disparity.size() * 3) {
                    if (planFinal_) { if (planDeferLocalAi_) localAiPending_ |= 2u; return none; }
                    mark_incomplete(__FILE__, __LINE__);
                    return none;
                }
                auto r = std::make_shared<ai::BgRaw>();
                r->prob = map->disparity;
                r->rgb = map->foregroundRgb;
                raw = std::move(r);
            }
            auto m = std::make_shared<std::vector<f32>>();
            ai::bg_matte(nullptr, *raw, nullptr, *m);
            bgStore_->put(matteKey, 0, *m);
            matte = std::move(m);
        }
        if (refine) {
            refined = bg_->cached_refined(matteKey, 0, refineKey);
            if (!refined) {
                ai::RotoImage image;
                ai::roto_image_from(px->rgba.data(), px->width, px->height, px->width * 4, 4, refineMaxSide, image);
                if (image.width) {
                    ai::RotoRefineSettings s = refineSettings;
                    const f32 side = static_cast<f32>(std::max(px->width, px->height));
                    if (side > 1.0f) s.radius *= static_cast<f32>(std::max(image.width, image.height)) / side;
                    s.radius = std::max(s.radius, 0.5f);
                    auto out = std::make_shared<ai::RotoRefined>();
                    ai::roto_refine(matte->data(), ai::kBgMatteSize, image, s, nullptr, *out);
                    bg_->put_refined(matteKey, 0, refineKey, *out);
                    refined = std::move(out);
                }
            }
        }
    } else {
        if (!asset->has_video() || !planMedia_ || !planMedia_->factory()) return none;
        const f64 fps = planComp_ && planComp_->fps() > 0.0 ? planComp_->fps() : 30.0;
        srcFps = asset->video.fps > 0.0 ? asset->video.fps : fps;
        const f64 srcFrame = l.source_frame(l.timeline_time(request.localTime));
        if (!std::isfinite(srcFrame)) return none;
        f64 idx = std::floor(srcFrame / fps * srcFps + 1e-3);
        if (asset->video.frameCount.value > 0) idx = std::min(idx, static_cast<f64>(asset->video.frameCount.value - 1));
        frame = static_cast<i64>(std::max(idx, 0.0));
        sourceTimeUs = static_cast<i64>(std::llround(static_cast<f64>(frame) * 1e6 / srcFps));
        matteKey = bg_key_of(*asset, true, model);
        ai::BgRemoveService::Source src;
        src.fg = foreground_.get();
        src.factory = planMedia_->factory();
        src.asset = *asset;
        src.rawKey = rawKey;
        src.matteKey = matteKey;
        src.model = model;
        src.fps = srcFps;
        src.frameCount = asset->video.frameCount.value > 0 ? asset->video.frameCount.value : frame + 1;
        src.layerW = static_cast<f32>(asset->video.width);
        src.layerH = static_cast<f32>(asset->video.height);
        src.refine = refineSettings;
        src.refineMaxSide = refineMaxSide;
        matte = bg_->cached(matteKey, frame);
        refined = refine && matte ? bg_->cached_refined(matteKey, frame, refineKey) : nullptr;
        if (!matte || (refine && !refined)) {
            if (wait) {
                // Export: o recorte exato deste quadro (e a borda fina), com prazo.
                ai::BgRemoveService::Result r = bg_->compute(src, frame, monotonic_ns() + 90'000'000'000ull);
                if (!r.matte) return none;
                matte = std::move(r.matte);
                refined = std::move(r.refined);
            } else {
                bg_->request(src, frame);
                if (planFinal_) { localAiPending_ |= 2u; return none; }
                mark_incomplete(__FILE__, __LINE__);
                if (!matte) {
                    // Segura um vizinho já pronto (tocando, o vídeo acompanha
                    // o instante dele; parado, fica no quadro atual).
                    for (i64 d = 1; d <= 6; ++d) {
                        for (const i64 f : {frame - d, frame + d}) {
                            if (f < 0) continue;
                            auto it = depthTex_.find(bmix(bmix(matteKey, static_cast<u64>(f)), 0xB6u));
                            if (it == depthTex_.end()) continue;
                            it->second.lastFrame = frameNumber_;
                            const i64 pinUs = planPlayback_ ? static_cast<i64>(std::llround(static_cast<f64>(f) * 1e6 / srcFps)) : -1;
                            return DepthMapResult{it->second.texture, 0.0f, 1.0f, false, pinUs};
                        }
                    }
                    return DepthMapResult{};
                }
                // Com o recorte pronto e a borda fina a caminho, a grade segura.
            }
        }
    }
    if (!matte) return none;
    const u64 texKey = bmix(bmix(matteKey, static_cast<u64>(frame)), 0xB6u);
    if (refined && refined->width && refined->height
        && refined->alpha.size() == static_cast<usize>(refined->width) * refined->height) {
        const bool rgba = refined->fg.size() == refined->alpha.size() * 3;
        const u64 key = bmix(bmix(texKey, refineKey), rgba ? 2u : 1u);
        TextureHandle tex{};
        if (auto it = rotoRefinedTex_.find(key); it != rotoRefinedTex_.end()) {
            it->second.lastFrame = frameNumber_;
            tex = it->second.texture;
        } else {
            while (rotoRefinedTex_.size() >= kRotoRefinedTexturesKept) {
                auto old = std::min_element(rotoRefinedTex_.begin(), rotoRefinedTex_.end(),
                                            [](const auto& a, const auto& b) { return a.second.lastFrame < b.second.lastFrame; });
                if (old->second.lastFrame >= frameNumber_) break;
                backend_->destroy_texture(old->second.texture);
                rotoRefinedTex_.erase(old);
            }
            TextureDesc d;
            d.width = refined->width; d.height = refined->height;
            d.format = rgba ? SurfaceFormat::RGBA8 : SurfaceFormat::R8;
            d.sampled = true; d.transferDst = true; d.debugName = "fundo-refinado";
            auto created = backend_->create_texture(d);
            if (!created.ok()) { mark_incomplete(__FILE__, __LINE__); return none; }
            PendingUpload up;
            up.texture = *created;
            const usize n = refined->alpha.size();
            up.bytesPerRow = refined->width * (rgba ? 4u : 1u);
            if (rgba) {
                up.data.resize(n * 4);
                for (usize i = 0; i < n; ++i) {
                    u8* t = &up.data[i * 4];
                    t[0] = refined->fg[i * 3]; t[1] = refined->fg[i * 3 + 1]; t[2] = refined->fg[i * 3 + 2];
                    t[3] = refined->alpha[i];
                }
            } else {
                up.data = refined->alpha;
            }
            uploads_.push_back(std::move(up));
            rotoRefinedTex_[key] = LutTexture{*created, frameNumber_};
            tex = *created;
        }
        DepthMapResult result{tex, 0.0f, 1.0f, false, sourceTimeUs};
        result.rotoRefined = rgba ? 2 : 1;
        return result;
    }
    TextureHandle tex{};
    if (auto it = depthTex_.find(texKey); it != depthTex_.end()) {
        it->second.lastFrame = frameNumber_;
        tex = it->second.texture;
    } else {
        if (depthTex_.size() >= 24) {
            for (auto old = depthTex_.begin(); old != depthTex_.end();) {
                if (old->second.lastFrame + 8 < frameNumber_) {
                    backend_->destroy_texture(old->second.texture);
                    old = depthTex_.erase(old);
                } else { ++old; }
            }
        }
        constexpr u32 kSize = ai::kBgMatteSize;
        TextureDesc d;
        d.width = kSize; d.height = kSize; d.format = SurfaceFormat::R16F;
        d.sampled = true; d.transferDst = true; d.debugName = "fundo-recorte";
        auto created = backend_->create_texture(d);
        if (!created.ok()) { mark_incomplete(__FILE__, __LINE__); return none; }
        PendingUpload up;
        up.texture = *created;
        up.bytesPerRow = kSize * 2;
        up.data.resize(static_cast<usize>(kSize) * kSize * 2);
        u16* texel = reinterpret_cast<u16*>(up.data.data());
        for (usize i = 0; i < matte->size() && i < static_cast<usize>(kSize) * kSize; ++i)
            texel[i] = scene3d::float_to_half(std::clamp((*matte)[i], 0.0f, 1.0f));
        uploads_.push_back(std::move(up));
        depthTex_[texKey] = LutTexture{*created, frameNumber_};
        tex = *created;
    }
    return DepthMapResult{tex, 0.0f, 1.0f, false, sourceTimeUs};
} catch (...) {
    mark_incomplete(__FILE__, __LINE__);
    return DepthMapResult{};
}

} // namespace aurea
