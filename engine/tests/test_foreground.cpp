#include "TestFramework.hpp"
#include "aurea/ai/ForegroundEstimator.hpp"
#include "aurea/ai/DepthMapService.hpp"
#include <cmath>
#include <filesystem>
#include "aurea/core/Log.hpp"
using namespace aurea;
AUREA_TEST(ForegroundAI, RealModelProducesDeterministicFiniteMasks) {
    set_log_sink([](LogLevel,const char* message,void*) {std::printf("\n    %s\n",message);},nullptr);
    std::filesystem::path root=std::filesystem::current_path();
    for(u32 i=0;i<8&&!std::filesystem::exists(root/"engine/assets/rotobrush/u2netp.bin");++i)root=root.parent_path();
    const auto directory=(root/"engine/assets/rotobrush").string();
    ai::ForegroundEstimator model;AUREA_CHECK(model.load(directory).ok());if(!model.loaded())return;
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
