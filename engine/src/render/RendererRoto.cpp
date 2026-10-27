// =============================================================================
//  Aurea / render / RendererRoto.cpp
//
//  O Rotobrush com traços (Roto Brush): em vez da probabilidade crua da rede,
//  o recorte refinado pelos traços e propagado pelo clipe (ai::RotoService).
//
//   - Preview: o que estiver no cache; o resto é pedido ao worker do Roto e
//     o quadro fica "incompleto" (volta quando o recorte ficar pronto).
//   - Export/captura: espera o recorte EXATO deste quadro (cadeia calculada
//     aqui, com prazo) — nunca exporta um recorte de outro quadro.
//   - A textura é a mesma R16F 320² do Rotobrush: o shader não muda de caminho.
// =============================================================================
#include "aurea/render/Renderer.hpp"

#include "aurea/ai/RotoService.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/scene3d/Environment.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace aurea {
namespace {

u64 rmix(u64 h, u64 v) noexcept {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    return h ^ (h >> 29);
}

u64 rhash(const std::string& s) noexcept {
    u64 h = 0xCBF29CE484222325ull;
    for (const char c : s) { h ^= static_cast<u8>(c); h *= 0x100000001B3ull; }
    return h;
}

/// A trilha do Roto: (camada, instância). A mesma conta do preview, do export
/// e do "Propagar clipe".
u64 roto_track(const Composition* comp, const Layer& layer, const EffectInstance* instance, const Asset* asset) noexcept {
    u64 layerKey = static_cast<u64>(reinterpret_cast<uintptr_t>(&layer));
    if (comp) {
        const OrderedIds<LayerId>& order = comp->order();
        for (u32 i = 0; i < order.size(); ++i)
            if (comp->layer(order.at(i)) == &layer) { layerKey = order.at(i).pack(); break; }
    }
    // Replacing media preserves layer/effect IDs and strokes. Its matte must
    // still be recomputed for the new source instead of reusing the old person.
    u64 source = layer.source.pack();
    if (asset) source = rmix(source, rhash(asset->sourcePath) ^ asset->contentHash);
    return rmix(rmix(rmix(layerKey, instance ? instance->id : 0u), source), 0x7070);
}

/// Fonte da camada para o Roto (quadro da fonte que a camada mostra agora).
struct RotoFrame {
    ai::RotoService::Source src;
    i64 frame = 0;
    i64 sourceTimeUs = -1;
    bool ok = false;
};

} // namespace

bool Renderer::roto_propagate(const Project& project, const Composition& comp, const Layer& layer,
                              const EffectInstance& instance, MediaManager* media) noexcept try {
    ai::RotoStrokes strokes;
    if (!ai::roto_instance_strokes(instance, strokes)) return false;
    if (layer.kind != LayerKind::Video || !media || !media->factory()) return false;
    const Asset* asset = project.asset(layer.source);
    if (!asset || !asset->has_video()) return false;
    if (!foreground_) foreground_ = std::make_unique<ai::DepthMapService>(true);
    // Bugs 0.3.4: "Propagar clipe" é pedido explícito — a rede que não carregou
    // antes tenta de novo (antes só depois de trocar de projeto).
    foreground_->retry_failed();
    if (!roto_) {
        roto_ = std::make_shared<ai::RotoService>();
        roto_->set_max_bytes(lowMemoryDevice_ ? ai::RotoService::kLowMemoryMaxBytes : ai::RotoService::kMaxBytes);
    }
    void (*wake)(void*) = nullptr; void* ctx = nullptr;
    media->ready_callback(wake, ctx);
    roto_->set_ready_callback(wake, ctx);
    ai::RotoService::Source src;
    src.fg = foreground_.get();
    src.factory = media->factory();
    src.asset = *asset;
    src.sourceKey = rmix(layer.source.pack() ^ 0xDA561CD33ull, rhash(asset->sourcePath) ^ asset->contentHash);
    src.fps = asset->video.fps > 0.0 ? asset->video.fps : (comp.fps() > 0.0 ? comp.fps() : 30.0);
    // Só o trecho da fonte que a camada usa (o clipe na timeline).
    const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
    const i64 total = asset->video.frameCount.value > 0 ? asset->video.frameCount.value
                                                        : std::max<i64>(1, std::llround(src.fps * 10));
    src.frameCount = total;
    src.layerW = static_cast<f32>(asset->video.width);
    src.layerH = static_cast<f32>(asset->video.height);
    // "Propagar clipe" já leva o Refinar borda (o refinado entra no cache junto).
    src.refine = ai::roto_instance_refine(instance);
    src.refineMaxSide = lowMemoryDevice_ ? 1280u : ai::kRotoRefineMaxSide;
    auto source_index = [&](i64 timelineTime) {
        const f64 sf = layer.source_frame(FrameIndex{timelineTime});
        if (!std::isfinite(sf)) return i64{0};
        return std::clamp<i64>(static_cast<i64>(std::floor(sf / fps * src.fps + 1e-3)), 0, total - 1);
    };
    i64 a = source_index(layer.start.value), b = source_index(std::max(layer.start.value, layer.end.value - 1));
    if (a > b) std::swap(a, b);
    // Os traços sempre entram no trecho propagado.
    for (const ai::RotoStroke& s : strokes) { a = std::min(a, s.frame); b = std::max(b, s.frame); }
    roto_->propagate(roto_track(&comp, layer, &instance, asset), strokes, src, std::max<i64>(0, a), std::min(total - 1, b));
    return true;
} catch (...) {
    return false;
}

DepthMapResult Renderer::roto_map(const DepthMapRequest& request, const std::vector<ai::RotoStroke>& strokes) noexcept try {
    DepthMapResult none; none.failed = planFinal_;
    constexpr u32 kSize = ai::RotoService::kSize;
    if (!backend_ || !request.host || !planProject_) return none;
    const Layer& l = *request.host;
    if (l.kind != LayerKind::Image && l.kind != LayerKind::Video) return none;
    const Asset* asset = planProject_->asset(l.source);
    if (!asset) return none;
    if (!foreground_) foreground_ = std::make_unique<ai::DepthMapService>(true);
    if (!roto_) {
        roto_ = std::make_shared<ai::RotoService>();
        roto_->set_max_bytes(lowMemoryDevice_ ? ai::RotoService::kLowMemoryMaxBytes : ai::RotoService::kMaxBytes);
    }
    if (planMedia_) {
        void (*wake)(void*) = nullptr; void* ctx = nullptr;
        planMedia_->ready_callback(wake, ctx);
        foreground_->set_ready_callback(wake, ctx);
        roto_->set_ready_callback(wake, ctx);
    }
    const bool wait = planFinal_ && !planDeferLocalAi_;
    const u64 sourceKey = rmix(l.source.pack() ^ 0xDA561CD33ull, rhash(asset->sourcePath) ^ asset->contentHash);
    const f32 chatter = std::clamp(request.smoothing, 0.0f, 1.0f);
    ai::RotoService::Source src;
    src.fg = foreground_.get();
    src.sourceKey = sourceKey;
    // Refinar borda: o raio vem em px da fonte; a classe LOW refina em 1280.
    src.refine.radius = std::isfinite(request.rotoEdgeRadius) ? std::max(0.0f, request.rotoEdgeRadius) : 0.0f;
    src.refine.chatter = chatter;
    src.refine.decontaminate = request.rotoDecontaminate;
    src.refineMaxSide = lowMemoryDevice_ ? 1280u : ai::kRotoRefineMaxSide;
    const bool refine = src.refine.on();
    i64 frame = 0, sourceTimeUs = -1;
    if (l.kind == LayerKind::Image) {
        const ImagePixels* px = planImageLookup_ ? planImageLookup_(planImageCtx_, l.source) : nullptr;
        if (!px || !px->width || !px->height || px->rgba.size() < static_cast<usize>(px->width) * px->height * 4)
            return none;
        // A mesma chave do Rotobrush sem traços: o mapa da rede é um só.
        const u64 frameKey = rmix(rmix(sourceKey, 0x1D), (static_cast<u64>(px->width) << 32) | px->height);
        src.image = foreground_->image(frameKey, px->rgba.data(), px->width, px->height, px->width * 4, 4, wait);
        src.layerW = static_cast<f32>(px->width);
        src.layerH = static_cast<f32>(px->height);
        src.frameCount = 1;
        if (refine) src.imageFrame = roto_->image_frame(frameKey, px->rgba.data(), px->width, px->height, px->width * 4,
                                                        src.refineMaxSide);
        if (!src.image) {
            if (planFinal_) { if (planDeferLocalAi_) localAiPending_ |= 2u; return none; }
            mark_incomplete(__FILE__, __LINE__);
            return none;
        }
    } else {
        if (!asset->has_video() || !planMedia_ || !planMedia_->factory()) return none;
        const f64 fps = planComp_ && planComp_->fps() > 0.0 ? planComp_->fps() : 30.0;
        const f64 srcFps = asset->video.fps > 0.0 ? asset->video.fps : fps;
        const f64 srcFrame = l.source_frame(l.timeline_time(request.localTime));
        if (!std::isfinite(srcFrame)) return none;
        f64 idx = std::floor(srcFrame / fps * srcFps + 1e-3);
        if (asset->video.frameCount.value > 0) idx = std::min(idx, static_cast<f64>(asset->video.frameCount.value - 1));
        frame = static_cast<i64>(std::max(idx, 0.0));
        sourceTimeUs = static_cast<i64>(std::llround(static_cast<f64>(frame) * 1e6 / srcFps));
        src.factory = planMedia_->factory();
        src.asset = *asset;
        src.fps = srcFps;
        src.frameCount = asset->video.frameCount.value > 0 ? asset->video.frameCount.value : frame + 1;
        src.layerW = static_cast<f32>(asset->video.width);
        src.layerH = static_cast<f32>(asset->video.height);
    }
    const u64 track = roto_track(planComp_, l, request.instance, asset);
    const u64 refineKey = refine ? src.refine.hash() : 0;
    auto tex_key = [&](i64 f) {
        const u64 k = rmix(rmix(track, static_cast<u64>(f)), ai::roto_dependency(strokes, f));
        return rmix(k, std::bit_cast<u32>(chatter));
    };
    ai::RotoService::MattePtr matte = roto_->cached(track, strokes, frame);
    ai::RotoService::RefinedPtr refined = refine && matte ? roto_->cached_refined(track, strokes, src.refine, frame) : nullptr;
    if (!matte || (refine && !refined)) {
        if (wait) {
            // Export: o recorte exato deste quadro, com prazo (a cadeia desde a base).
            ai::RotoService::Result r = roto_->compute_all(track, strokes, src, frame, monotonic_ns() + 90'000'000'000ull);
            if (!r.matte) return none;
            matte = std::move(r.matte);
            refined = std::move(r.refined);
            // Refino que falhou (quadro ilegível na resolução cheia): a grade, sem
            // inventar borda — o export não trava por isso.
        } else {
            roto_->request(track, strokes, src, frame);
            if (planFinal_) { localAiPending_ |= 2u; return none; }
            mark_incomplete(__FILE__, __LINE__);
            // Com o recorte da grade pronto e o refinado a caminho, a grade
            // segura o quadro (a borda fina chega no próximo desenho).
            if (!matte) {
                // Preview: segura a textura de um quadro vizinho já pronto (melhor
                // atrasado que piscando o original). TOCANDO, o vídeo acompanha o
                // instante dele (recorte e imagem casam); PARADO/scrub, o vídeo
                // fica no quadro atual com o recorte aproximado - prender o vídeo
                // num quadro antigo era o "quadro congelado" ao pintar.
                for (i64 d = 1; d <= 6 && l.kind == LayerKind::Video; ++d) {
                    for (const i64 f : {frame - d, frame + d}) {
                        if (f < 0) continue;
                        auto it = depthTex_.find(tex_key(f));
                        if (it == depthTex_.end()) continue;
                        it->second.lastFrame = frameNumber_;
                        const i64 pinUs = planPlayback_ ? static_cast<i64>(std::llround(static_cast<f64>(f) * 1e6 / src.fps)) : -1;
                        return DepthMapResult{it->second.texture, 0.0f, 1.0f, false, pinUs};
                    }
                }
                return none;
            }
        }
    }
    if (refined && refined->width && refined->height && refined->alpha.size() == static_cast<usize>(refined->width) * refined->height) {
        // Refinado: R8 (só o alfa) ou RGBA8 (cor do objeto na borda + alfa),
        // na resolução do quadro do refino. Poucos vivos (até 8 MB cada).
        const bool rgba = refined->fg.size() == refined->alpha.size() * 3;
        const u64 key = rmix(rmix(tex_key(frame), refineKey), rgba ? 2u : 1u);
        TextureHandle tex{};
        if (auto it = rotoRefinedTex_.find(key); it != rotoRefinedTex_.end()) {
            it->second.lastFrame = frameNumber_;
            tex = it->second.texture;
        } else {
            while (rotoRefinedTex_.size() >= kRotoRefinedTexturesKept) {
                // Sai a usada há mais tempo — nunca uma deste quadro (ainda no grafo).
                auto old = std::min_element(rotoRefinedTex_.begin(), rotoRefinedTex_.end(),
                                            [](const auto& a, const auto& b) { return a.second.lastFrame < b.second.lastFrame; });
                if (old->second.lastFrame >= frameNumber_) break;
                backend_->destroy_texture(old->second.texture);   // destruição adiada até a GPU largar
                rotoRefinedTex_.erase(old);
            }
            TextureDesc d;
            d.width = refined->width; d.height = refined->height;
            d.format = rgba ? SurfaceFormat::RGBA8 : SurfaceFormat::R8;
            d.sampled = true; d.transferDst = true; d.debugName = "roto-refinado";
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
    // Reduzir trepidação: média com os vizinhos já calculados.
    std::vector<f32> values;
    const std::vector<f32>* use = matte.get();
    if (chatter > 0.0f && l.kind == LayerKind::Video) {
        const auto prev = roto_->cached(track, strokes, frame - 1);
        const auto next = roto_->cached(track, strokes, frame + 1);
        ai::roto_reduce_chatter(*matte, prev.get(), next.get(), chatter, values);
        use = &values;
    }
    const u64 texKey = tex_key(frame);
    TextureHandle tex{};
    if (auto it = depthTex_.find(texKey); it != depthTex_.end()) {
        it->second.lastFrame = frameNumber_;
        tex = it->second.texture;
    } else {
        // Use the same bounded GPU map cache as automatic foreground/depth.
        // Propagated mattes can be reconstructed from the bounded CPU cache;
        // keeping one GPU texture for every visited frame grows without limit.
        if (depthTex_.size() >= 24) {
            for (auto old = depthTex_.begin(); old != depthTex_.end();) {
                if (old->second.lastFrame + 8 < frameNumber_) {
                    backend_->destroy_texture(old->second.texture);
                    old = depthTex_.erase(old);
                } else { ++old; }
            }
        }
        TextureDesc d;
        d.width = kSize; d.height = kSize; d.format = SurfaceFormat::R16F;
        d.sampled = true; d.transferDst = true; d.debugName = "roto-recorte";
        auto created = backend_->create_texture(d);
        if (!created.ok()) { mark_incomplete(__FILE__, __LINE__); return none; }
        PendingUpload up;
        up.texture = *created;
        up.bytesPerRow = kSize * 2;
        up.data.resize(static_cast<usize>(kSize) * kSize * 2);
        u16* texel = reinterpret_cast<u16*>(up.data.data());
        for (usize i = 0; i < use->size() && i < static_cast<usize>(kSize) * kSize; ++i)
            texel[i] = scene3d::float_to_half(std::clamp((*use)[i], 0.0f, 1.0f));
        uploads_.push_back(std::move(up));
        depthTex_[texKey] = LutTexture{*created, frameNumber_};
        tex = *created;
    }
    return DepthMapResult{tex, 0.0f, 1.0f, false, sourceTimeUs};
} catch (...) {
    mark_incomplete(__FILE__, __LINE__);
    return DepthMapResult{};
}

std::shared_ptr<const std::vector<f32>> Renderer::roto_cached_matte(const Composition* comp, const Layer& layer,
                                                                   const EffectInstance& instance, i64 frame, const Asset* asset) noexcept try {
    if (!roto_) return nullptr;
    std::vector<ai::RotoStroke> strokes;
    if (!ai::roto_instance_strokes(instance, strokes) || strokes.empty()) return nullptr;
    return roto_->cached(roto_track(comp, layer, &instance, asset), strokes, std::max<i64>(0, frame));
} catch (...) {
    return nullptr;
}

bool Renderer::roto_status(const Project& project, const Composition& comp, const Layer& layer,
                           const EffectInstance& instance, i64 sourceFrame, i64* out8) noexcept try {
    if (!out8) return false;
    for (u32 i = 0; i < 8; ++i) out8[i] = 0;
    ai::RotoStrokes strokes;
    if (!ai::roto_instance_strokes(instance, strokes) || strokes.empty()) return false;
    out8[7] = 1;
    const Asset* asset = project.asset(layer.source);
    const u64 track = roto_track(&comp, layer, &instance, asset);
    i64 first = 0, last = 0;
    if (layer.kind == LayerKind::Video && asset && asset->has_video()) {
        const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
        const f64 srcFps = asset->video.fps > 0.0 ? asset->video.fps : fps;
        const i64 total = asset->video.frameCount.value > 0 ? asset->video.frameCount.value
                                                            : std::max<i64>(1, std::llround(srcFps * 10));
        auto source_index = [&](i64 timelineTime) {
            const f64 sf = layer.source_frame(FrameIndex{timelineTime});
            if (!std::isfinite(sf)) return i64{0};
            return std::clamp<i64>(static_cast<i64>(std::floor(sf / fps * srcFps + 1e-3)), 0, total - 1);
        };
        first = source_index(layer.start.value);
        last = source_index(std::max(layer.start.value, layer.end.value - 1));
        if (first > last) std::swap(first, last);
        for (const ai::RotoStroke& s : strokes) { first = std::min(first, s.frame); last = std::max(last, s.frame); }
        first = std::max<i64>(0, first); last = std::min(total - 1, last);
    }
    out8[6] = last - first + 1;
    if (!roto_) return true;   // traços sem serviço ainda: nada calculado
    const auto p = roto_->progress();
    // Bugs 0.3.4: [3] leva o motivo da falha (≠ 0 continua sendo "falhou").
    if (p.track == track) {
        out8[0] = p.done; out8[1] = p.total; out8[2] = p.running ? 1 : 0;
        out8[3] = p.failed ? std::max<i64>(1, static_cast<i64>(p.reason)) : 0;
    }
    // Com Refinar borda, "pronto" é o refinado também.
    const u64 refineKey = ai::roto_instance_refine(instance).hash();
    out8[4] = roto_->ready(track, strokes, sourceFrame, refineKey) ? 1 : 0;
    out8[5] = roto_->ready_count(track, strokes, first, last, refineKey);
    return true;
} catch (...) {
    return false;
}

} // namespace aurea
