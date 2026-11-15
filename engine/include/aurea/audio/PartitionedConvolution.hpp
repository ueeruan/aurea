#pragma once

#include "aurea/core/Result.hpp"
#include <array>
#include <complex>
#include <memory>
#include <span>
#include <vector>

namespace aurea::audio {

// Immutable coefficients shared by instances of the same audio plugin.
struct ConvolutionKernel {
    static constexpr u32 kDirectFrames = 128;
    struct Partition {
        u32 block = 0, offset = 0, count = 0;
        std::array<std::vector<std::complex<f32>>, 2> spectra;
    };
    std::array<std::array<f32, kDirectFrames>, 2> direct{};
    std::vector<Partition> partitions;
    u32 frames = 0;
    u64 immutableBytes = 0, instanceBytes = 0;
};

// Stereo interleaved coefficients, already converted to the host sample rate.
[[nodiscard]] Result<std::shared_ptr<const ConvolutionKernel>> prepare_convolution(
    std::span<const f32> impulse, u32 channels, u32 maxFrames) noexcept;

// A direct head and progressively larger FFT partitions provide zero added
// latency. Every allocation belongs to prepare(), outside process().
class PartitionedConvolver {
public:
    [[nodiscard]] Status prepare(std::shared_ptr<const ConvolutionKernel> kernel) noexcept;
    void process(f32 left, f32 right, f32& outLeft, f32& outRight) noexcept;
private:
    struct PartitionState {
        std::array<std::vector<std::complex<f32>>, 2> inputSpectra;
        std::array<std::vector<f32>, 2> input, overlap, output;
        std::vector<std::complex<f32>> transform, sum;
        u32 fill = 0;
        u64 blocks = 0;
    };
    std::shared_ptr<const ConvolutionKernel> kernel_;
    std::array<std::array<f32, ConvolutionKernel::kDirectFrames>, 2> direct_{};
    std::vector<PartitionState> partitions_;
    u64 time_ = 0;
};
}
