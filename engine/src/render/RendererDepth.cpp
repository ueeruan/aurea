// =============================================================================
//  Aurea / render / RendererDepth.cpp
//
//  EffectResources::depth_map do renderer: o mapa de profundidade (IA) da
//  FONTE de uma camada no instante do `prepare` em curso.
//
//   - A rede roda uma vez por quadro-fonte (ai::DepthMapService). A chave é
//     (asset, caminho, quadro da fonte) — o preview e o export pedem a mesma.
//   - O mapa sobe como R16F 256×256, normalizado pelos percentis 2º/98º do
//     PRÓPRIO quadro; o shader amplia na GPU.
//   - Os limites que o shader usa são os percentis suavizados no tempo (EMA),
//     por instância do efeito: quadros seguidos da mesma fonte andam devagar;
//     corte, seek ou salto recomeçam dos percentis do quadro. Export e preview
//     têm estados separados — o export não herda o que o preview tocou.
// =============================================================================
#include "aurea/render/Renderer.hpp"

#include "aurea/project/Project.hpp"
#include "aurea/scene3d/Environment.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace {

u64 mix64(u64 h, u64 v) noexcept {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    return h ^ (h >> 29);
}

u64 text_hash(const std::string& s) noexcept {
    u64 h = 0xCBF29CE484222325ull;
    for (const char c : s) {
        h ^= static_cast<u8>(c);
        h *= 0x100000001B3ull;
    }
    return h;
}

/// Um quadro solto (seek, corte) não suaviza: recomeça dos percentis dele.
constexpr i64 kDepthMaxStep = 2;
/// Texturas de mapa guardadas antes de soltar as que ninguém usa (~3 MB).
constexpr usize kDepthTexturesKept = 24;

/// O intervalo que normaliza a textura de um quadro (percentis 2º/98º dele).
f32 depth_range(const ai::DepthMap& map) noexcept {
    return std::max(map.p98 - map.p2, 1e-6f * std::max(1.0f, std::fabs(map.p98)));
}

/// A disparidade normalizada pelos percentis do próprio quadro, em half (f16
/// guarda bem 0..1 e o que passa um pouco das pontas).
std::vector<u8> depth_texels(const ai::DepthMap& map) {
    const f32 p2 = map.p2, range = depth_range(map);
    std::vector<u8> data(map.disparity.size() * 2);
    u16* px = reinterpret_cast<u16*>(data.data());
    for (u32 i = 0; i < map.disparity.size(); ++i)
        px[i] = scene3d::float_to_half(std::clamp((map.disparity[i] - p2) / range, -4.0f, 5.0f));
    return data;
}

TextureDesc depth_texture_desc(u32 size = ai::DepthEstimator::kSize) noexcept {
    TextureDesc d;
    d.width = size;
    d.height = size;
    d.format = SurfaceFormat::R16F;
    d.sampled = true;
    d.transferDst = true;
    d.debugName = "mapa-profundidade";
    return d;
}

} // namespace

DepthMapResult Renderer::depth_map(const DepthMapRequest& request) noexcept {
    DepthMapResult none; none.failed = request.foreground && planFinal_;
    auto& service = request.foreground ? foreground_ : depth_;
    const u32 mapSize = request.foreground ? ai::ForegroundEstimator::kSize : ai::DepthEstimator::kSize;
    if (!backend_) return none;
    if (!request.host) return request.foreground ? none : depth_map_preview();
    if (!planProject_) return none;
    const Layer& l = *request.host;
    if (l.kind != LayerKind::Image && l.kind != LayerKind::Video) return none;
    const Asset* asset = planProject_->asset(l.source);
    if (!asset) return none;
    if (!service) service = std::make_unique<ai::DepthMapService>(request.foreground ? foregroundModelDirectory_ : std::string{});
    if (planMedia_) {
        void (*wake)(void*) = nullptr; void* ctx = nullptr;
        planMedia_->ready_callback(wake, ctx); service->set_ready_callback(wake, ctx);
    }

    const u64 assetKey = l.source.pack() ^ (request.foreground ? 0xDA561CD33ull : 0);
    const u64 sourceKey = mix64(assetKey, text_hash(asset->sourcePath) ^ asset->contentHash);
    u64 frameKey = 0;
    i64 frameIndex = 0;
    bool video = false;
    ai::DepthMapPtr map;
    if (l.kind == LayerKind::Image) {
        const ImagePixels* px = planImageLookup_ ? planImageLookup_(planImageCtx_, l.source) : nullptr;
        if (!px || !px->width || !px->height || px->rgba.size() < static_cast<usize>(px->width) * px->height * 4)
            return none;
        frameKey = mix64(mix64(sourceKey, 0x1D), (static_cast<u64>(px->width) << 32) | px->height);
        map = service->image(frameKey, px->rgba.data(), px->width, px->height, px->width * 4, 4, !request.foreground || planFinal_);
    } else {
        if (!asset->has_video() || !planMedia_ || !planMedia_->factory()) return none;
        video = true;
        // O quadro da fonte que a camada mostra agora: a MESMA conta do
        // prepare (tempo da fonte → grade de quadros da fonte, piso).
        const f64 fps = planComp_ && planComp_->fps() > 0.0 ? planComp_->fps() : 30.0;
        const f64 srcFps = asset->video.fps > 0.0 ? asset->video.fps : fps;
        const f64 srcFrame = l.source_frame(l.timeline_time(request.localTime));
        if (!std::isfinite(srcFrame)) return none;
        f64 idx = std::floor(srcFrame / fps * srcFps + 1e-3);
        if (asset->video.frameCount.value > 0) idx = std::min(idx, static_cast<f64>(asset->video.frameCount.value - 1));
        idx = std::max(idx, 0.0);
        frameIndex = static_cast<i64>(idx);
        frameKey = mix64(mix64(sourceKey, 0x2D), static_cast<u64>(frameIndex));
        void (*wake)(void*) = nullptr;
        void* wakeCtx = nullptr;
        planMedia_->ready_callback(wake, wakeCtx);
        service->set_ready_callback(wake, wakeCtx);
        const i64 targetUs = static_cast<i64>(std::llround(idx * 1e6 / srcFps));
        const i64 frameUs = static_cast<i64>(std::llround(1e6 / srcFps));
        map = service->video(frameKey, planMedia_->factory(), *asset, sourceKey, targetUs, frameUs, planFinal_);
    }

    // O estado da suavização: (camada, efeito), export separado do preview.
    u64 layerKey = static_cast<u64>(reinterpret_cast<uintptr_t>(request.host));
    if (planComp_) {
        const OrderedIds<LayerId>& order = planComp_->order();
        for (u32 i = 0; i < order.size(); ++i) {
            if (planComp_->layer(order.at(i)) == request.host) { layerKey = order.at(i).pack(); break; }
        }
    }
    const u64 stateKey = mix64(mix64(layerKey, request.instance ? request.instance->id : 0u), planFinal_ ? 1u : 0u);
    DepthState& st = depthState_[stateKey];
    st.lastFrame = frameNumber_;

    if (!map && video && !planFinal_ && !request.foreground) {
        // Preview de vídeo ainda calculando: o render volta quando o worker
        // terminar. No PLAY a rede leva mais que um quadro — quando o mapa do
        // quadro N fica pronto o preview já pede o N+5 —, então vale o mapa
        // pronto mais recente DESTA fonte (atrasado alguns quadros), não só o
        // último que esta instância já mostrou: no play esse era nenhum, e o
        // efeito nunca aparecia.
        incomplete_ = true;
        u64 latestKey = 0;
        i64 latestUs = 0, latestFrameUs = 1;
        if (ai::DepthMapPtr latest = service->latest_video(sourceKey, latestKey, latestUs, latestFrameUs);
            latest && latestKey != st.frameKey) {
            map = std::move(latest);
            frameKey = latestKey;
            frameIndex = latestFrameUs > 0 ? (latestUs + latestFrameUs / 2) / latestFrameUs : frameIndex;
        }
    }
    if (!map) {
        if (request.foreground) { incomplete_ = !planFinal_; return none; }
        // O último mapa pronto desta instância segura o quadro (melhor
        // atrasado que piscando o original).
        if (auto it = depthTex_.find(st.frameKey); st.frameKey && it != depthTex_.end()) {
            it->second.lastFrame = frameNumber_;
            return DepthMapResult{it->second.texture, st.texLo, st.texHi};
        }
        return none;
    }

    // A textura deste quadro, normalizada pelos percentis dele. Sobe com os
    // outros uploads do quadro (antes do grafo).
    const f32 p2 = map->p2, range = depth_range(*map);
    TextureHandle tex{};
    if (auto it = depthTex_.find(frameKey); it != depthTex_.end()) {
        it->second.lastFrame = frameNumber_;
        tex = it->second.texture;
    } else {
        // Vídeo tocando cria uma textura (128 KB) por quadro-fonte: acima de
        // um punhado, sai a que nenhum quadro em voo usa mais.
        if (depthTex_.size() >= kDepthTexturesKept) {
            for (auto old = depthTex_.begin(); old != depthTex_.end();) {
                if (old->second.lastFrame + 8 < frameNumber_) {
                    backend_->destroy_texture(old->second.texture);
                    old = depthTex_.erase(old);
                } else {
                    ++old;
                }
            }
        }
        auto created = backend_->create_texture(depth_texture_desc(mapSize));
        if (!created.ok()) return none;
        PendingUpload up;
        up.texture = *created;
        up.bytesPerRow = mapSize * 2;
        up.data = depth_texels(*map);
        uploads_.push_back(std::move(up));
        depthTex_[frameKey] = LutTexture{*created, frameNumber_};
        tex = *created;
    }

    // Suavização: só quando a fonte ANDOU (o mesmo quadro de novo — pausa,
    // redesenho — não conta duas vezes).
    if (st.frameKey != frameKey) {
        const bool follows = video && st.frameKey != 0 && st.asset == sourceKey
                          && std::llabs(frameIndex - st.frame) <= kDepthMaxStep;
        if (follows) {
            const f32 a = 1.0f - 0.9f * std::clamp(request.smoothing, 0.0f, 1.0f);
            st.lo += a * (map->p2 - st.lo);
            st.hi += a * (map->p98 - st.hi);
        } else {
            st.lo = map->p2;
            st.hi = map->p98;
        }
        st.frameKey = frameKey;
        st.asset = sourceKey;
        st.frame = frameIndex;
        st.texLo = (st.lo - p2) / range;
        st.texHi = (st.hi - p2) / range;
        if (!(st.texHi > st.texLo + 1e-4f)) { st.texLo = 0.0f; st.texHi = 1.0f; }
    }
    return DepthMapResult{tex, st.texLo, st.texHi};
}

DepthMapResult Renderer::depth_map_preview() noexcept {
    // A prévia do catálogo (render_effect_preview): a rede roda sobre a foto
    // das prévias RECORTADA exatamente como a cartela (a mesma conta do passe
    // "foto-recorte"), então o mapa cobre a cartela inteira. Sem foto, a
    // cartela sintética não tem profundidade: o efeito devolve a entrada.
    DepthMapResult none;
    if (previewSrc_.empty() || !previewSrcW_ || !previewSrcH_ || !compTargetW_ || !compTargetH_) return none;
    const f32 src = static_cast<f32>(previewSrcW_) / static_cast<f32>(previewSrcH_);
    const f32 dst = static_cast<f32>(compTargetW_) / static_cast<f32>(compTargetH_);
    f32 mx = 1.0f, my = 1.0f, ox = 0.0f, oy = 0.0f;   // uv da foto = uv * m + o
    if (dst > src) { my = src / dst; oy = (1.0f - my) * 0.35f; }
    else           { mx = dst / src; ox = (1.0f - mx) * 0.5f; }
    const u32 x0 = std::min(previewSrcW_ - 1, static_cast<u32>(ox * static_cast<f32>(previewSrcW_)));
    const u32 y0 = std::min(previewSrcH_ - 1, static_cast<u32>(oy * static_cast<f32>(previewSrcH_)));
    const u32 cw = std::clamp(static_cast<u32>(std::lround(mx * static_cast<f32>(previewSrcW_))), 1u, previewSrcW_ - x0);
    const u32 ch = std::clamp(static_cast<u32>(std::lround(my * static_cast<f32>(previewSrcH_))), 1u, previewSrcH_ - y0);
    // A foto pode ser trocada: a chave leva uma amostra dos bytes e o recorte.
    u64 key = mix64(0x9D, (static_cast<u64>(previewSrcW_) << 32) | previewSrcH_);
    for (usize i = 0; i < previewSrc_.size(); i += 97) key = mix64(key, previewSrc_[i]);
    key = mix64(mix64(key, (static_cast<u64>(x0) << 32) | y0), (static_cast<u64>(cw) << 32) | ch);
    if (auto it = depthTex_.find(key); it != depthTex_.end()) {
        it->second.lastFrame = frameNumber_;
        return DepthMapResult{it->second.texture, 0.0f, 1.0f};
    }
    if (!depth_) depth_ = std::make_unique<ai::DepthMapService>();
    const u8* origin = previewSrc_.data() + (static_cast<usize>(y0) * previewSrcW_ + x0) * 4;
    const ai::DepthMapPtr map = depth_->image(key, origin, cw, ch, previewSrcW_ * 4, 4);
    if (!map) return none;
    const std::vector<u8> texels = depth_texels(*map);
    auto created = backend_->create_texture(depth_texture_desc());
    if (!created.ok()) return none;
    // A prévia não passa pelo render(): sobe direto, como a foto das prévias.
    if (!backend_->upload_texture(*created, texels.data(), ai::DepthEstimator::kSize * 2).ok()) {
        backend_->destroy_texture(*created);
        return none;
    }
    depthTex_[key] = LutTexture{*created, frameNumber_};
    return DepthMapResult{*created, 0.0f, 1.0f};
}

u32 Renderer::collect_depth(u64 frameNumber, u64 idleFrames) noexcept {
    u32 n = 0;
    for (auto it = depthTex_.begin(); it != depthTex_.end();) {
        if (frameNumber > it->second.lastFrame + idleFrames) {
            if (backend_) backend_->destroy_texture(it->second.texture);
            ++n;
            it = depthTex_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = depthState_.begin(); it != depthState_.end();) {
        if (frameNumber > it->second.lastFrame + idleFrames) it = depthState_.erase(it);
        else ++it;
    }
    return n;
}

void Renderer::release_depth(bool destroyTextures) noexcept {
    if (destroyTextures && backend_) {
        for (auto& [k, t] : depthTex_) backend_->destroy_texture(t.texture);
    }
    depthTex_.clear();
    depthState_.clear();
    if (depth_) depth_->clear();
    if (foreground_) foreground_->clear();
}

} // namespace aurea
