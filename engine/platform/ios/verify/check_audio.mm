// Exercise the real Apple planar source-node contract and sample-rate conversion.
// Runs on the macOS CI host without a sound device or an iPhone.
#import <AVFoundation/AVFoundation.h>
#include "aurea/audio/PlanarOutput.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

struct Tone { uint64_t sample = 0; };
static void renderTone(void* ctx, float* out, uint32_t frames) noexcept {
    auto& tone = *static_cast<Tone*>(ctx);
    for (uint32_t i = 0; i < frames; ++i, ++tone.sample) {
        const double t = double(tone.sample) / 48000.0;
        out[2*i] = float(.25 * std::sin(2 * M_PI * 440 * t));
        out[2*i+1] = float(.5 * std::sin(2 * M_PI * 660 * t));
    }
}

static bool run(double rate) {
    AVAudioEngine* engine = [AVAudioEngine new];
    AVAudioFormat* sourceFormat = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:48000 channels:2];
    AVAudioFormat* outputFormat = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:rate channels:2];
    Tone tone;
    Tone* state = &tone;
    __block bool layoutOK = true;
    AVAudioSourceNode* source = [[AVAudioSourceNode alloc] initWithFormat:sourceFormat renderBlock:
        ^OSStatus(BOOL* silent, const AudioTimeStamp*, AVAudioFrameCount frames, AudioBufferList* data) {
            if (data->mNumberBuffers != 2 || data->mBuffers[0].mNumberChannels != 1 || data->mBuffers[1].mNumberChannels != 1) {
                layoutOK = false; return kAudio_ParamError;
            }
            const bool ok = aurea::audio::render_planar_stereo(renderTone, state,
                static_cast<float*>(data->mBuffers[0].mData), data->mBuffers[0].mDataByteSize,
                static_cast<float*>(data->mBuffers[1].mData), data->mBuffers[1].mDataByteSize, frames);
            layoutOK = layoutOK && ok; *silent = !ok; return noErr;
        }];
    [engine attachNode:source];
    [engine connect:source to:engine.mainMixerNode format:sourceFormat];
    NSError* error = nil;
    if (![engine enableManualRenderingMode:AVAudioEngineManualRenderingModeOffline format:outputFormat maximumFrameCount:1024 error:&error]
        || ![engine startAndReturnError:&error]) {
        std::fprintf(stderr, "Audio engine: %s\n", error.localizedDescription.UTF8String); return false;
    }
    AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:outputFormat frameCapacity:1024];
    std::vector<float> channels[2];
    for (int attempt = 0; channels[0].size() < size_t(rate * 2) && attempt < 500; ++attempt) {
        const auto status = [engine renderOffline:1024 toBuffer:buffer error:&error];
        if (status == AVAudioEngineManualRenderingStatusCannotDoInCurrentContext) continue;
        if (status != AVAudioEngineManualRenderingStatusSuccess) {
            std::fprintf(stderr, "Offline audio status %ld\n", (long)status); [engine stop]; return false;
        }
        for (int c = 0; c < 2; ++c) channels[c].insert(channels[c].end(), buffer.floatChannelData[c], buffer.floatChannelData[c] + buffer.frameLength);
    }
    [engine stop];
    if (!layoutOK || channels[0].size() < size_t(rate * 2)) return false;
    for (int c = 0; c < 2; ++c) {
        const auto& samples = channels[c];
        double sum = 0; size_t crossings = 0;
        const size_t start = 4096; // Ignore converter startup latency.
        for (size_t i = start; i < samples.size(); ++i) {
            if (!std::isfinite(samples[i]) || std::fabs(samples[i]) > .51) return false;
            if (samples[i-1] <= 0 && samples[i] > 0) ++crossings;
            sum += samples[i] * samples[i];
        }
        const double seconds = (samples.size() - start) / rate;
        const double frequency = crossings / seconds;
        const double rms = std::sqrt(sum / (samples.size() - start));
        const double expectedFrequency = c ? 660 : 440;
        const double expectedRMS = (c ? .5 : .25) / std::sqrt(2.0);
        std::printf("Apple audio %.0f Hz channel %d: frequency %.3f, RMS %.6f\n", rate, c, frequency, rms);
        if (std::fabs(frequency - expectedFrequency) > 2 || std::fabs(rms - expectedRMS) > .005) return false;
    }
    return true;
}

int main() { @autoreleasepool { return run(48000) && run(44100) ? 0 : 1; } }
