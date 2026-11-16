#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/audio/PartitionedConvolution.hpp"
#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace aurea::json { struct Value; class Writer; }
namespace aurea::plugins {
struct ParamDecl;

inline constexpr u32 kAudioMaxNodes = 64;
inline constexpr u32 kAudioMaxInputs = 8;
inline constexpr u32 kAudioMaxBlockFrames = 4096;
inline constexpr u32 kAudioSampleRate = 48000;
inline constexpr u32 kAudioChannels = 2;
inline constexpr u32 kAudioMaxImpulseFrames = 2 * kAudioSampleRate;
inline constexpr u32 kAudioMaxDelayMs = 8000;
inline constexpr u64 kAudioMaxStateBytes = 32ull << 20;
inline constexpr u64 kAudioMaxImpulseFileBytes = 16ull << 20;

enum class AudioNodeKind : u8 {
    Input, Gain, Mix, Delay, Allpass, Lowpass, Highpass, Equalizer,
    Compressor, Limiter, Convolution,
};
struct AudioControl {
    i32 parameter = -1;
    f32 value = 0.f, scale = 1.f, offset = 0.f;
};
struct AudioNode {
    std::string id;
    AudioNodeKind kind = AudioNodeKind::Input;
    std::vector<u32> inputs;
    std::array<AudioControl, kAudioMaxInputs> controls{};
    u32 delayLimitMs = 0;
    i32 impulse = -1;
};
struct AudioImpulseFile { std::string id, file; };
struct AudioGraph {
    std::vector<AudioNode> nodes;       // topologically sorted at admission
    std::vector<AudioImpulseFile> impulses;
    std::vector<f32> defaults, minima, maxima;
    u32 output = 0;
    u64 fingerprint = 0;
    std::shared_ptr<const json::Value> source;
};
struct AudioProgram {
    std::shared_ptr<const AudioGraph> graph;
    std::vector<std::shared_ptr<const audio::ConvolutionKernel>> impulses;
    u64 fingerprint = 0;
    u64 stateBytes = 0, immutableBytes = 0;
    u32 prerollFrames = 0;
};

[[nodiscard]] bool parse_audio_graph(const json::Value& value, const std::vector<ParamDecl>& params,
                                     std::shared_ptr<const AudioGraph>& output, std::string& error);
void write_audio_graph(json::Writer& writer, const AudioGraph& graph);
[[nodiscard]] Result<std::shared_ptr<const AudioProgram>> prepare_audio_program(
    std::shared_ptr<const AudioGraph> graph, const std::string& directory) noexcept;

// Input/output are interleaved float PCM in the host's canonical format.
// prepare() owns all allocation, IR I/O and FFT preparation. process_block()
// executes a bounded graph, retaining its state across consecutive blocks.
class AudioProcessor {
public:
    AudioProcessor();
    ~AudioProcessor();
    AudioProcessor(AudioProcessor&&) noexcept;
    AudioProcessor& operator=(AudioProcessor&&) noexcept;
    AudioProcessor(const AudioProcessor&) = delete;
    AudioProcessor& operator=(const AudioProcessor&) = delete;
    [[nodiscard]] Status prepare(std::shared_ptr<const AudioProgram> program,
                                 u32 maxBlockFrames = kAudioMaxBlockFrames) noexcept;
    [[nodiscard]] Status process_block(const f32* input, f32* output, u32 frames,
        u32 sampleRate, u32 channels, std::span<const f32> parameters) noexcept;
    [[nodiscard]] u64 state_bytes() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
