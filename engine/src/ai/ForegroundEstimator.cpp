#include "aurea/ai/ForegroundEstimator.hpp"
#include "aurea/ai/ForegroundMatte.hpp"
#include "aurea/core/Time.hpp"
#include <net.h>
#include <datareader.h>
#include <algorithm>
#include <cmath>
#include <new>
#include <vector>
#include "aurea/core/Log.hpp"
namespace aurea::ai {
namespace embedded {
const unsigned char* foreground_model_weights();
const char* foreground_model_param();
}
namespace {
class InferenceAllocator final : public ncnn::Allocator {
public:
    void* fastMalloc(size_t size) override {
        constexpr size_t budget=192u*1024u*1024u;
        if(size>budget-64||live_>budget-size-64){AUREA_LOG_WARN("Rotobrush allocation budget: live=%zu request=%zu",live_,size);return nullptr;}
        auto* base=static_cast<unsigned char*>(ncnn::fastMalloc(size+64));if(!base)return nullptr;
        *reinterpret_cast<size_t*>(base)=size+64;live_+=size+64;peak_=std::max(peak_,live_);return base+64;
    }
    void fastFree(void* ptr) override {if(!ptr)return;auto* base=static_cast<unsigned char*>(ptr)-64;live_-=*reinterpret_cast<size_t*>(base);ncnn::fastFree(base);}
    size_t peak() const {return peak_;}
private:size_t live_=0,peak_=0;
};
}
struct ForegroundEstimator::Impl { ncnn::Net net; bool ready=false; f32 ms=0; };
ForegroundEstimator::ForegroundEstimator() : impl_(new(std::nothrow) Impl) {}
ForegroundEstimator::~ForegroundEstimator() = default;
bool ForegroundEstimator::loaded() const noexcept { return impl_ && impl_->ready; }
f32 ForegroundEstimator::last_inference_ms() const noexcept { return impl_ ? impl_->ms : 0; }
void ForegroundEstimator::unload() noexcept { if(impl_) {impl_->net.clear();impl_->ready=false;} }
Status ForegroundEstimator::load() try {
    if(!impl_) return Errc::OutOfMemory; unload();
    auto& net=impl_->net;
    net.opt.num_threads=1;net.opt.use_vulkan_compute=false;net.opt.lightmode=true;
    net.opt.use_fp16_storage=false;net.opt.use_fp16_arithmetic=false;net.opt.use_bf16_storage=false;
    // Large Winograd/im2col workspaces compete with the editor on 32-bit devices.
    net.opt.use_winograd_convolution=false;net.opt.use_sgemm_convolution=false;
    const unsigned char* weights=embedded::foreground_model_weights();
    ncnn::DataReaderFromMemory reader(weights);
    if(net.load_param_mem(embedded::foreground_model_param()) || net.load_model(reader))
        return Status{Errc::CorruptData,"Rotobrush: bundled model could not be loaded"};
    impl_->ready=true; return OkStatus;
} catch(const std::bad_alloc&) {return Errc::OutOfMemory;}
Status ForegroundEstimator::run(const u8* pixels,u32 w,u32 h,u32 stride,u32 channels,const std::atomic<bool>& cancel,f32* out) try {
    if(!loaded()) return Errc::InvalidState;
    if(!pixels || !out || !w || !h || w>16384 || h>16384 || (channels!=3 && channels!=4) || stride<w*channels) return Errc::InvalidArgument;
    if(cancel.load()) return Errc::Cancelled;
    std::vector<u8> rgb;
    foreground_rgb(pixels,w,h,stride,channels,kSize,rgb);
    InferenceAllocator allocator;
    auto input=ncnn::Mat::from_pixels(rgb.data(),ncnn::Mat::PIXEL_RGB,kSize,kSize,&allocator);
    if(input.empty()) return Errc::OutOfMemory;
    // Same RGB/ImageNet normalization as the published U2Net inference.
    const float maxPixel=std::max(1.f,static_cast<float>(*std::max_element(rgb.begin(),rgb.end())));
    const float mean[]={.485f*maxPixel,.456f*maxPixel,.406f*maxPixel};
    const float norm[]={1.f/(.229f*maxPixel),1.f/(.224f*maxPixel),1.f/(.225f*maxPixel)};
    input.substract_mean_normalize(mean,norm);
    auto ex=impl_->net.create_extractor(); ex.set_blob_allocator(&allocator); ex.set_workspace_allocator(&allocator); ncnn::Mat output;
    const auto start=monotonic_ns();
    const int inputResult=ex.input("in0",input);
    const int outputResult=inputResult ? inputResult : ex.extract("out0",output);
    impl_->ms=static_cast<f32>((monotonic_ns()-start)*1e-6);
    AUREA_LOG_INFO("Rotobrush inference %.1f ms peak %.1f MiB",impl_->ms,allocator.peak()/1048576.0);
    if(outputResult || output.empty()) { AUREA_LOG_WARN("Rotobrush inference input=%d output=%d dims=%d,%d,%d",inputResult,outputResult,output.w,output.h,output.c); return Errc::OutOfMemory; }
    if(output.w!=kSize || output.h!=kSize || output.c!=1) return Errc::CorruptData;
    if(cancel.load()) return Errc::Cancelled;
    for(u32 i=0;i<kPixels;++i) out[i]=foreground_probability(output.row(i/kSize)[i%kSize]);
    return OkStatus;
} catch(const std::bad_alloc&) {return Errc::OutOfMemory;}
}
