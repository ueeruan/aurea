#include "TestFramework.hpp"
#include "aurea/ai/ForegroundEstimator.hpp"
#include "aurea/ai/ForegroundMatte.hpp"
#include "aurea/ai/DepthMapService.hpp"
#include "aurea/ai/RotoService.hpp"
#include <chrono>
#include <cmath>
#include <thread>
#include <filesystem>
#include "aurea/core/Log.hpp"
#include "SyntheticVideo.hpp"
using namespace aurea;
AUREA_TEST(ForegroundAI, Regression2131ResizeFiltersFineDetailAcrossTheWholeImage) {
    std::vector<u8> image(640*960*4,255), rgb;
    for(u32 y=0;y<960;++y)for(u32 x=0;x<640;++x) {
        auto* p=&image[(y*640+x)*4];p[0]=(x&1)?255:0;p[1]=u8(y*255/959);p[2]=u8(x*255/639);
    }
    ai::foreground_rgb(image.data(),640,960,640*4,4,320,rgb);
    AUREA_CHECK_EQ(rgb.size(),320u*320u*3u);
    for(u32 y=0;y<320;++y)for(u32 x=0;x<320;++x) {
        AUREA_CHECK(std::abs(int(rgb[(y*320+x)*3])-128)<=1);
    }
    AUREA_CHECK(rgb[1]<2);AUREA_CHECK(rgb.back()>253);
    AUREA_CHECK(rgb[(319*320)*3+1]>253);
    AUREA_CHECK(ai::foreground_probability(.002f)<.01f);
    AUREA_CHECK_EQ(ai::foreground_probability(std::numeric_limits<f32>::quiet_NaN()),0.f);
}

AUREA_TEST(ForegroundAI, Regression2131TemporalMaskRejectsFlashesAndSceneCuts) {
    constexpr u32 n=32;
    std::vector<f32> previous(n*n,0),current(n*n,0),next(n*n,0),out;
    std::vector<u8> rgb(n*n*3,20),other;
    for(u32 y=8;y<24;++y)for(u32 x=8;x<24;++x) {
        previous[y*n+x]=next[y*n+x]=.98f;current[y*n+x]=.98f;
        for(u32 c=0;c<3;++c)rgb[(y*n+x)*3+c]=180;
    }
    // One frame loses the lower half of a static subject and spuriously adds a corner.
    for(u32 y=16;y<24;++y)for(u32 x=8;x<24;++x)current[y*n+x]=.01f;
    current[0]=.99f;
    ai::stabilize_foreground(current,rgb,previous,rgb,next,rgb,n,out);
    AUREA_CHECK(out==previous);
    std::vector<f32> reverse;
    ai::stabilize_foreground(current,rgb,next,rgb,previous,rgb,n,reverse);
    AUREA_CHECK(out==reverse);
    // New scene: unrelated neighbours must not restore the old subject.
    other.assign(rgb.size(),255);
    ai::stabilize_foreground(current,rgb,previous,other,next,other,n,out);
    AUREA_CHECK(out==current);
    // A newly exposed background pixel must not inherit foreground coverage.
    other=rgb;for(u32 c=0;c<3;++c)rgb[(16*n+16)*3+c]=20;
    current[16*n+16]=0;
    ai::stabilize_foreground(current,rgb,previous,other,next,other,n,out);
    AUREA_CHECK_EQ(out[16*n+16],0.f);
}
AUREA_TEST(ForegroundAI, RealModelProducesDeterministicFiniteMasks) {
    set_log_sink([](LogLevel,const char* message,void*) {std::printf("\n    %s\n",message);},nullptr);
    ai::ForegroundEstimator model;AUREA_CHECK(model.load().ok());if(!model.loaded())return;
    std::vector<u8> pixels(320*320*4,255);
    for(u32 y=0;y<320;++y)for(u32 x=0;x<320;++x){const bool inside=(int(x)-160)*(int(x)-160)+(int(y)-160)*(int(y)-160)<6400;auto* p=&pixels[(y*320+x)*4];p[0]=inside?220:30;p[1]=inside?40:120;p[2]=inside?30:70;}
    std::vector<f32> first(ai::ForegroundEstimator::kPixels),second(first.size());std::atomic<bool> cancel{false};
    const auto status=model.run(pixels.data(),320,320,1280,4,cancel,first.data());
    std::printf("\n    U2Net status: %u %s\n",static_cast<u32>(status.code()),status.message().data());AUREA_CHECK(status.ok());
    std::printf("\n    U2Net-P CPU: %.1f ms\n",model.last_inference_ms());
    AUREA_CHECK(model.run(pixels.data(),320,320,1280,4,cancel,second.data()).ok());
    AUREA_CHECK(first==second);f32 lo=1,hi=0;for(auto v:first){if(!std::isfinite(v)||v<0||v>1){AUREA_CHECK(false);break;}lo=std::min(lo,v);hi=std::max(hi,v);}
    AUREA_CHECK(hi-lo>.8f);AUREA_CHECK(first[160*320+160]>.5f);AUREA_CHECK(first[0]<.5f);
    cancel=true;AUREA_CHECK_EQ(model.run(pixels.data(),320,320,1280,4,cancel,second.data()).code(),Errc::Cancelled);
    model.unload();AUREA_CHECK(!model.loaded());set_log_sink(nullptr,nullptr);
}

AUREA_TEST(ForegroundAI, Regression2131FinalVideoFrameWorksWithoutContainerFrameCount) {
    aurea::test::SyntheticConfig cfg;cfg.width=96;cfg.height=64;cfg.frameCount=2;
    cfg.pattern=aurea::test::SyntheticPattern::MovingSquare;
    aurea::test::SyntheticFactory factory(cfg);
    Asset asset;asset.kind=AssetKind::Video;asset.video.width=96;asset.video.height=64;asset.video.fps=30;
    ai::DepthMapService service(true);
    const auto last=service.video(1001,&factory,asset,101,33333,33333,true);
    AUREA_CHECK(last);if(!last)return;
    AUREA_CHECK_EQ(last->disparity.size(),ai::ForegroundEstimator::kPixels);
    service.clear();
    const auto reversed=service.video(1001,&factory,asset,101,33333,33333,true);
    AUREA_CHECK(reversed);if(reversed)AUREA_CHECK(reversed->disparity==last->disparity);
}

// =============================================================================
//  Bugs 0.3.4: "Rotobrush não inicia, mesmo depois de seguir o aviso 'a
//  propagação falhou'". Um decoder que deu erro (codec do aparelho tomado, estado
//  de erro do MediaCodec) era reaproveitado para sempre pelo DepthMapService: toda
//  tentativa seguinte falhava e "pinte mais um traço ou propague de novo" nunca
//  funcionava. Agora o decoder com erro sai, o próximo pedido abre outro, e o
//  status diz o motivo (3 = leitura do vídeo).
// =============================================================================
namespace {
/// Decoder do aparelho em estado de erro: seek "funciona", nenhum quadro sai.
class DeadDecoder final : public VideoDecoderBackend {
public:
    explicit DeadDecoder(const VideoStreamInfo& info) : info_(info) {}
    const VideoStreamInfo& info() const noexcept override { return info_; }
    Status seek_to_keyframe(i64) noexcept override { return OkStatus; }
    Status next_frame(i64, FrameRef&, i64&, bool& eos) noexcept override {
        eos = false;
        return Status{Errc::DecodeFailed, "codec em estado de erro"};
    }
private:
    VideoStreamInfo info_;
};

/// Abre decoders mortos enquanto `dead` > 0 (cada abertura gasta um), depois sintéticos.
class FlakyFactory final : public VideoSourceFactory {
public:
    explicit FlakyFactory(const aurea::test::SyntheticConfig& c) : inner_(c) {}
    bool probe(const char* path, MediaProbe& out) override { return inner_.probe(path, out); }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset& asset, MediaPriority priority) override {
        ++opened;
        if (dead.load() > 0) {
            dead.fetch_sub(1);
            aurea::test::SyntheticDecoder probe(cfg());
            return std::make_unique<DeadDecoder>(probe.info());
        }
        return inner_.open_video(asset, priority);
    }
    std::atomic<i32> dead{0};
    std::atomic<u32> opened{0};
private:
    aurea::test::SyntheticConfig cfg() const {
        aurea::test::SyntheticConfig c; c.width = 96; c.height = 64; c.frameCount = 6;
        c.pattern = aurea::test::SyntheticPattern::MovingSquare;
        return c;
    }
    aurea::test::SyntheticFactory inner_;
};

aurea::test::SyntheticConfig flaky_config() {
    aurea::test::SyntheticConfig c; c.width = 96; c.height = 64; c.frameCount = 6;
    c.pattern = aurea::test::SyntheticPattern::MovingSquare;
    return c;
}

Asset flaky_asset() {
    Asset a; a.kind = AssetKind::Video; a.video.width = 96; a.video.height = 64; a.video.fps = 30;
    a.video.frameCount.value = 6;
    return a;
}

bool wait_roto_idle(const ai::RotoService& roto, u32 ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until) {
        if (!roto.progress().running) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
} // namespace

AUREA_TEST(ForegroundAI, Bugs034DecoderInErrorIsReopenedOnTheNextRequest) {
    FlakyFactory factory(flaky_config());
    factory.dead = 1;
    const Asset asset = flaky_asset();
    ai::DepthMapService service(true);
    // 1º pedido: o decoder aberto está morto — falha, com o motivo "leitura".
    const auto first = service.foreground_video_frame(&factory, asset, 0xAB, 0, 30.0);
    AUREA_CHECK(!first);
    AUREA_CHECK_EQ(service.last_failure(), static_cast<u32>(ai::DepthMapService::kFailDecode));
    // 2º pedido (o usuário seguiu o aviso): antes reaproveitava o morto e falhava
    // para sempre; agora abre outro decoder e o mapa sai.
    const auto second = service.foreground_video_frame(&factory, asset, 0xAB, 0, 30.0);
    AUREA_CHECK(second);
    AUREA_CHECK_EQ(factory.opened.load(), 2u);
    if (second) AUREA_CHECK_EQ(second->disparity.size(), ai::ForegroundEstimator::kPixels);
}

AUREA_TEST(ForegroundAI, Bugs034FailedPropagationReportsTheReasonAndPropagatingAgainWorks) {
    FlakyFactory factory(flaky_config());
    factory.dead = 1000;   // toda abertura morta até o "aparelho" liberar o codec
    const Asset asset = flaky_asset();
    ai::DepthMapService fg(true);
    ai::RotoService roto;
    ai::RotoService::Source src;
    src.fg = &fg; src.factory = &factory; src.asset = asset; src.sourceKey = 0xCD;
    src.fps = 30.0; src.frameCount = 6; src.layerW = 96; src.layerH = 64;
    ai::RotoStroke s; s.frame = 0; s.radius = 0.1f; s.points = {Vec2{0.5f, 0.5f}};
    const ai::RotoStrokes strokes{s};
    roto.propagate(77, strokes, src, 0, 2);
    AUREA_CHECK(wait_roto_idle(roto, 20'000));
    const auto failed = roto.progress();
    AUREA_CHECK(failed.failed);
    AUREA_CHECK_EQ(failed.reason, static_cast<u32>(ai::DepthMapService::kFailDecode));
    // O codec volta; "Propagar clipe" de novo funciona e o motivo some.
    factory.dead = 0;
    fg.retry_failed();
    roto.propagate(77, strokes, src, 0, 2);
    AUREA_CHECK(wait_roto_idle(roto, 60'000));
    const auto again = roto.progress();
    AUREA_CHECK(!again.failed);
    AUREA_CHECK_EQ(again.reason, 0u);
    AUREA_CHECK_EQ(again.done, i64{3});
    AUREA_CHECK(roto.ready(77, strokes, 2, 0));
}
