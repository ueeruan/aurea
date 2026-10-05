#include "TestFramework.hpp"
#include "aurea/ai/ForegroundEstimator.hpp"
#include "aurea/ai/ForegroundMatte.hpp"
#include "aurea/ai/DepthMapService.hpp"
#include <cmath>
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
