#pragma once
#include "aurea/core/Result.hpp"
#include <atomic>
#include <memory>
#include <string>
namespace aurea::ai {
class ForegroundEstimator {
public:
    static constexpr u32 kSize=320, kPixels=kSize*kSize;
    ForegroundEstimator(); ~ForegroundEstimator();
    [[nodiscard]] Status load(const std::string& directory);
    [[nodiscard]] Status run(const u8*,u32,u32,u32,u32,const std::atomic<bool>&,f32*);
    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] f32 last_inference_ms() const noexcept;
    void unload() noexcept;
private: struct Impl; std::unique_ptr<Impl> impl_;
};
}
