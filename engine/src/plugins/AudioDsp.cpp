#include "aurea/plugins/AudioDsp.hpp"
#include "aurea/audio/AudioEffects.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea::plugins {
namespace {
constexpr f64 pi = 3.14159265358979323846;
f32 control_value(const AudioGraph& graph, const AudioControl& control, std::span<const f32> parameters) noexcept {
    f32 value = control.value;
    if (control.parameter >= 0) {
        const usize index = static_cast<usize>(control.parameter);
        value = index < parameters.size() && std::isfinite(parameters[index]) ? parameters[index] : graph.defaults[index];
        value = std::clamp(value, graph.minima[index], graph.maxima[index]);
    }
    return value * control.scale + control.offset;
}
struct FilterState {
    f64 z1[2]{}, z2[2]{};
    f32 sample(const audio::Biquad& coefficients, u32 channel, f32 input) noexcept {
        const f64 output = coefficients.b0 * input + z1[channel];
        z1[channel] = coefficients.b1 * input - coefficients.a1 * output + z2[channel];
        z2[channel] = coefficients.b2 * input - coefficients.a2 * output;
        if (std::fabs(z1[channel]) < 1e-25) z1[channel] = 0;
        if (std::fabs(z2[channel]) < 1e-25) z2[channel] = 0;
        return static_cast<f32>(output);
    }
};
struct NodeState {
    std::vector<f32> delay;
    u32 position = 0;
    f32 damping[2]{};
    f32 envelope = 0.f;
    FilterState filter;
    audio::PartitionedConvolver convolution;
};
}

struct AudioProcessor::Impl {
    std::shared_ptr<const AudioProgram> program;
    std::vector<NodeState> nodes;
    std::vector<f32> scratch;
    u32 maxFrames = 0;
    u64 bytes = 0;
    bool failed = false;
};

AudioProcessor::AudioProcessor() : impl_(std::make_unique<Impl>()) {}
AudioProcessor::~AudioProcessor() = default;
AudioProcessor::AudioProcessor(AudioProcessor&&) noexcept = default;
AudioProcessor& AudioProcessor::operator=(AudioProcessor&&) noexcept = default;
u64 AudioProcessor::state_bytes() const noexcept { return impl_ ? impl_->bytes : 0; }

Status AudioProcessor::prepare(std::shared_ptr<const AudioProgram> program, u32 maxBlockFrames) noexcept {
    if (!program || !program->graph || program->graph->nodes.empty() || !maxBlockFrames
        || maxBlockFrames > kAudioMaxBlockFrames || program->stateBytes > kAudioMaxStateBytes)
        return Status{Errc::BudgetExceeded, "audio plugin state or block budget exceeded"};
    auto next = std::make_unique<Impl>();
    next->program = std::move(program); next->maxFrames = maxBlockFrames;
    const auto& graph = *next->program->graph;
    if (graph.nodes.size() > kAudioMaxNodes || graph.output >= graph.nodes.size()
        || graph.defaults.size() != graph.minima.size() || graph.defaults.size() != graph.maxima.size()) return Errc::InvalidArgument;
    for (usize index = 0; index < graph.nodes.size(); ++index) {
        const auto& node = graph.nodes[index];
        if (node.inputs.size() > kAudioMaxInputs || node.delayLimitMs > kAudioMaxDelayMs
            || static_cast<u32>(node.kind) > static_cast<u32>(AudioNodeKind::Convolution)) return Errc::InvalidArgument;
        for (u32 source : node.inputs) if (source >= index) return Errc::InvalidArgument;
        for (const auto& control : node.controls)
            if ((control.parameter >= 0 && static_cast<usize>(control.parameter) >= graph.defaults.size())
                || !std::isfinite(control.value) || !std::isfinite(control.scale) || !std::isfinite(control.offset)) return Errc::InvalidArgument;
    }
    next->nodes.resize(graph.nodes.size());
    next->scratch.resize(static_cast<usize>(maxBlockFrames) * kAudioChannels * graph.nodes.size());
    next->bytes = next->scratch.size() * sizeof(f32) + next->nodes.size() * sizeof(NodeState);
    for (usize index = 0; index < graph.nodes.size(); ++index) {
        const auto& node = graph.nodes[index];
        auto& state = next->nodes[index];
        if (node.kind == AudioNodeKind::Delay || node.kind == AudioNodeKind::Allpass) {
            const u32 frames = node.delayLimitMs * (kAudioSampleRate / 1000) + 4;
            state.delay.assign(static_cast<usize>(frames) * kAudioChannels, 0.f);
            next->bytes += state.delay.size() * sizeof(f32);
        }
        if (node.kind == AudioNodeKind::Convolution) {
            if (node.impulse < 0 || static_cast<usize>(node.impulse) >= next->program->impulses.size())
                return Status{Errc::InvalidArgument, "audio impulse is unavailable"};
            const auto& kernel = next->program->impulses[static_cast<usize>(node.impulse)];
            if (const auto status = state.convolution.prepare(kernel); !status.ok()) return status;
            next->bytes += kernel->instanceBytes;
        }
    }
    if (next->bytes > kAudioMaxStateBytes) return Errc::BudgetExceeded;
    impl_ = std::move(next);
    return OkStatus;
}

Status AudioProcessor::process_block(const f32* input, f32* output, u32 frames,
    u32 sampleRate, u32 channels, std::span<const f32> parameters) noexcept {
    if (!impl_ || !impl_->program || !input || !output || !frames || frames > impl_->maxFrames
        || sampleRate != kAudioSampleRate || channels != kAudioChannels)
        return Status{Errc::InvalidArgument, "audio block must use admitted stereo 48000 Hz PCM dimensions"};
    auto& instance = *impl_;
    if (instance.failed) return Status{Errc::InvalidState, "audio graph must be reset after a numeric failure"};
    const auto& graph = *instance.program->graph;
    const usize count = static_cast<usize>(frames) * channels;
    const usize stride = static_cast<usize>(instance.maxFrames) * channels;
    for (usize index = 0; index < graph.nodes.size(); ++index) {
        const auto& node = graph.nodes[index];
        auto& state = instance.nodes[index];
        f32* destination = instance.scratch.data() + index * stride;
        const f32* source = node.inputs.empty() ? input : instance.scratch.data() + node.inputs.front() * stride;
        std::array<f32, kAudioMaxInputs> values{};
        for (u32 parameter = 0; parameter < values.size(); ++parameter)
            values[parameter] = control_value(graph, node.controls[parameter], parameters);
        switch (node.kind) {
            case AudioNodeKind::Input:
                for (usize i = 0; i < count; ++i) destination[i] = std::isfinite(input[i]) ? input[i] : 0.f;
                break;
            case AudioNodeKind::Gain:
                for (usize i = 0; i < count; ++i) destination[i] = source[i] * values[0];
                break;
            case AudioNodeKind::Mix:
                std::fill(destination, destination + count, 0.f);
                for (usize connection = 0; connection < node.inputs.size(); ++connection) {
                    const f32* samples = instance.scratch.data() + node.inputs[connection] * stride;
                    for (usize i = 0; i < count; ++i) destination[i] += samples[i] * values[connection];
                }
                break;
            case AudioNodeKind::Delay:
            case AudioNodeKind::Allpass: {
                const u32 capacity = static_cast<u32>(state.delay.size() / channels);
                const f32 delay = std::clamp(values[0] * (kAudioSampleRate / 1000.f), 1.f, static_cast<f32>(capacity - 3));
                const u32 whole = static_cast<u32>(delay);
                const f32 fraction = delay - whole;
                const f32 feedback = std::clamp(values[1], -.999f, .999f);
                const f32 smoothing = node.kind == AudioNodeKind::Allpass ? 1.f
                    : static_cast<f32>(1.0 - std::exp(-2.0 * pi * std::clamp(values[2], 5.f, 23000.f) / kAudioSampleRate));
                for (u32 frame = 0; frame < frames; ++frame) {
                    const u32 first = (state.position + capacity - whole) % capacity;
                    const u32 second = (first + capacity - 1) % capacity;
                    for (u32 channel = 0; channel < channels; ++channel) {
                        const usize at = static_cast<usize>(frame) * channels + channel;
                        const f32 delayed = state.delay[first * channels + channel] * (1.f - fraction)
                            + state.delay[second * channels + channel] * fraction;
                        state.damping[channel] += smoothing * (delayed - state.damping[channel]);
                        const f32 result = node.kind == AudioNodeKind::Allpass ? delayed - feedback * source[at] : delayed;
                        state.delay[state.position * channels + channel] = source[at]
                            + feedback * (node.kind == AudioNodeKind::Allpass ? result : state.damping[channel]);
                        destination[at] = result;
                    }
                    state.position = (state.position + 1) % capacity;
                }
                break;
            }
            case AudioNodeKind::Lowpass:
            case AudioNodeKind::Highpass:
            case AudioNodeKind::Equalizer: {
                const f32 frequency = std::clamp(values[0], 5.f, 23000.f);
                const auto coefficients = node.kind == AudioNodeKind::Equalizer
                    ? audio::eq_band(frequency, 100.0 / std::clamp(values[1], .1f, 20.f), std::clamp(values[2], -48.f, 48.f))
                    : audio::pass_filter(node.kind == AudioNodeKind::Highpass, frequency);
                for (u32 frame = 0; frame < frames; ++frame)
                    for (u32 channel = 0; channel < channels; ++channel) {
                        const usize at = static_cast<usize>(frame) * channels + channel;
                        destination[at] = state.filter.sample(coefficients, channel, source[at]);
                    }
                break;
            }
            case AudioNodeKind::Compressor:
            case AudioNodeKind::Limiter: {
                const bool limiter = node.kind == AudioNodeKind::Limiter;
                const f32 threshold = std::clamp(values[0], -80.f, 0.f);
                const f32 ratio = limiter ? 1000.f : std::clamp(values[1], 1.f, 100.f);
                const f32 attack = limiter || values[2] <= 0.f ? 0.f
                    : static_cast<f32>(std::exp(-1.0 / (kAudioSampleRate * values[2] * .001)));
                const f32 release = static_cast<f32>(std::exp(-1.0 / (kAudioSampleRate * std::max(1.f, values[3]) * .001)));
                const f32 knee = limiter ? 0.f : std::clamp(values[4], 0.f, 24.f);
                const f32 makeup = std::pow(10.f, std::clamp(values[5], -24.f, 24.f) / 20.f);
                const f32 ceiling = std::pow(10.f, threshold / 20.f);
                for (u32 frame = 0; frame < frames; ++frame) {
                    const usize at = static_cast<usize>(frame) * channels;
                    const f32 peak = std::max(std::fabs(source[at]), std::fabs(source[at + 1]));
                    const f32 smoothing = peak > state.envelope ? attack : release;
                    state.envelope = smoothing * state.envelope + (1.f - smoothing) * peak;
                    const f32 above = 20.f * std::log10(std::max(1e-12f, state.envelope)) - threshold;
                    f32 reduction = 0.f;
                    if (knee > 0.f && above > -knee * .5f && above < knee * .5f)
                        reduction = (1.f / ratio - 1.f) * (above + knee * .5f) * (above + knee * .5f) / (2.f * knee);
                    else if (above >= knee * .5f) reduction = (1.f / ratio - 1.f) * above;
                    const f32 gain = makeup * std::pow(10.f, reduction / 20.f);
                    for (u32 channel = 0; channel < channels; ++channel) {
                        const f32 value = source[at + channel] * gain;
                        destination[at + channel] = limiter ? std::clamp(value, -ceiling, ceiling) : value;
                    }
                }
                break;
            }
            case AudioNodeKind::Convolution:
                for (u32 frame = 0; frame < frames; ++frame) {
                    const usize at = static_cast<usize>(frame) * channels;
                    f32 left = 0.f, right = 0.f;
                    state.convolution.process(source[at], source[at + 1], left, right);
                    const f32 wet = std::clamp(values[0], 0.f, 1.f);
                    destination[at] = source[at] * (1.f - wet) + left * wet;
                    destination[at + 1] = source[at + 1] * (1.f - wet) + right * wet;
                }
                break;
        }
        for (usize i = 0; i < count; ++i) if (!std::isfinite(destination[i])) {
            instance.failed = true;
            return Status{Errc::InvalidArgument, "audio plugin produced a nonfinite sample"};
        }
    }
    std::memmove(output, instance.scratch.data() + graph.output * stride, count * sizeof(f32));
    return OkStatus;
}
}
