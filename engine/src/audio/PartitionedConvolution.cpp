#include "aurea/audio/PartitionedConvolution.hpp"
#include "aurea/audio/Fft.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::audio {

Result<std::shared_ptr<const ConvolutionKernel>> prepare_convolution(
    std::span<const f32> impulse, u32 channels, u32 maxFrames) noexcept {
    if ((channels != 1 && channels != 2) || impulse.empty() || impulse.size() % channels
        || impulse.size() / channels > maxFrames)
        return Status{Errc::InvalidArgument, "invalid convolution impulse dimensions"};
    for (f32 sample : impulse) if (!std::isfinite(sample))
        return Status{Errc::InvalidArgument, "nonfinite convolution impulse"};
    auto kernel = std::make_shared<ConvolutionKernel>();
    kernel->frames = static_cast<u32>(impulse.size() / channels);
    for (u32 channel = 0; channel < 2; ++channel)
        for (u32 frame = 0; frame < std::min(kernel->frames, ConvolutionKernel::kDirectFrames); ++frame)
            kernel->direct[channel][frame] = impulse[static_cast<usize>(frame) * channels + std::min(channel, channels - 1)];
    struct Region { u32 block, offset, end; };
    for (const Region region : {Region{128, 128, 1024}, Region{512, 1024, 4096}, Region{2048, 4096, maxFrames}}) {
        if (kernel->frames <= region.offset) continue;
        ConvolutionKernel::Partition partition;
        partition.block = region.block; partition.offset = region.offset;
        const u32 end = std::min(kernel->frames, region.end);
        partition.count = (end - region.offset + region.block - 1) / region.block;
        const u32 fftSize = 2 * partition.block;
        for (u32 channel = 0; channel < 2; ++channel) {
            auto& spectra = partition.spectra[channel];
            spectra.resize(static_cast<usize>(partition.count) * fftSize);
            for (u32 part = 0; part < partition.count; ++part) {
                auto* values = spectra.data() + static_cast<usize>(part) * fftSize;
                const u32 first = region.offset + part * region.block;
                for (u32 i = 0; i < region.block && first + i < end; ++i)
                    values[i] = {impulse[static_cast<usize>(first + i) * channels + std::min(channel, channels - 1)], 0.f};
                fft_complex(values, fftSize);
            }
            kernel->immutableBytes += spectra.size() * sizeof(std::complex<f32>);
        }
        kernel->instanceBytes += static_cast<u64>(partition.count) * fftSize * 2 * sizeof(std::complex<f32>)
            + static_cast<u64>(partition.offset + 4 * partition.block) * 2 * sizeof(f32)
            + static_cast<u64>(fftSize) * 2 * sizeof(std::complex<f32>);
        kernel->partitions.push_back(std::move(partition));
    }
    kernel->immutableBytes += sizeof(ConvolutionKernel);
    kernel->instanceBytes += 2 * ConvolutionKernel::kDirectFrames * sizeof(f32);
    return std::shared_ptr<const ConvolutionKernel>(std::move(kernel));
}

Status PartitionedConvolver::prepare(std::shared_ptr<const ConvolutionKernel> kernel) noexcept {
    if (!kernel || !kernel->frames) return Errc::InvalidArgument;
    kernel_ = std::move(kernel);
    time_ = 0; direct_ = {};
    partitions_.clear(); partitions_.resize(kernel_->partitions.size());
    for (usize index = 0; index < partitions_.size(); ++index) {
        const auto& definition = kernel_->partitions[index];
        auto& state = partitions_[index];
        const u32 fftSize = 2 * definition.block;
        for (u32 channel = 0; channel < 2; ++channel) {
            state.inputSpectra[channel].assign(static_cast<usize>(definition.count) * fftSize, {});
            state.input[channel].assign(definition.block, 0.f);
            state.overlap[channel].assign(definition.block, 0.f);
            state.output[channel].assign(definition.offset + 2 * definition.block, 0.f);
        }
        state.transform.assign(fftSize, {}); state.sum.assign(fftSize, {});
    }
    return OkStatus;
}

void PartitionedConvolver::process(f32 left, f32 right, f32& outLeft, f32& outRight) noexcept {
    if (!kernel_) { outLeft = left; outRight = right; return; }
    const f32 inputs[2] = {left, right};
    f32 result[2]{};
    constexpr u32 directFrames = ConvolutionKernel::kDirectFrames;
    const u32 directPosition = static_cast<u32>(time_ % directFrames);
    for (u32 channel = 0; channel < 2; ++channel) {
        direct_[channel][directPosition] = inputs[channel];
        for (u32 tap = 0; tap < std::min(kernel_->frames, directFrames); ++tap)
            result[channel] += kernel_->direct[channel][tap] * direct_[channel][(directPosition + directFrames - tap) % directFrames];
    }
    for (usize index = 0; index < partitions_.size(); ++index) {
        const auto& definition = kernel_->partitions[index];
        auto& state = partitions_[index];
        const u32 fftSize = 2 * definition.block;
        for (u32 channel = 0; channel < 2; ++channel) {
            const usize position = time_ % state.output[channel].size();
            result[channel] += state.output[channel][position];
            state.output[channel][position] = 0.f;
            state.input[channel][state.fill] = inputs[channel];
        }
        if (++state.fill != definition.block) continue;
        state.fill = 0;
        const u32 slot = static_cast<u32>(state.blocks % definition.count);
        const u32 live = static_cast<u32>(std::min<u64>(state.blocks + 1, definition.count));
        for (u32 channel = 0; channel < 2; ++channel) {
            std::fill(state.transform.begin(), state.transform.end(), std::complex<f32>{});
            for (u32 i = 0; i < definition.block; ++i) state.transform[i] = {state.input[channel][i], 0.f};
            fft_complex(state.transform.data(), fftSize);
            auto& inputSpectra = state.inputSpectra[channel];
            std::copy(state.transform.begin(), state.transform.end(), inputSpectra.begin() + static_cast<usize>(slot) * fftSize);
            std::fill(state.sum.begin(), state.sum.end(), std::complex<f32>{});
            for (u32 part = 0; part < live; ++part) {
                const u32 inputSlot = (slot + definition.count - part) % definition.count;
                const auto* input = inputSpectra.data() + static_cast<usize>(inputSlot) * fftSize;
                const auto* coefficients = definition.spectra[channel].data() + static_cast<usize>(part) * fftSize;
                for (u32 bin = 0; bin < fftSize; ++bin) state.sum[bin] += input[bin] * coefficients[bin];
            }
            fft_complex(state.sum.data(), fftSize, true);
            const u64 first = state.blocks * definition.block + definition.offset;
            auto& output = state.output[channel];
            for (u32 i = 0; i < definition.block; ++i) {
                output[(first + i) % output.size()] += state.sum[i].real() + state.overlap[channel][i];
                state.overlap[channel][i] = state.sum[i + definition.block].real();
            }
        }
        ++state.blocks;
    }
    ++time_;
    outLeft = result[0]; outRight = result[1];
}
}
