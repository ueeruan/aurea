#include "TestFramework.hpp"
#include "../src/plugins/PluginInternal.hpp"
#include "aurea/audio/Audio.hpp"
#include "aurea/audio/Fft.hpp"
#include "aurea/project/Presets.hpp"
#include "aurea/Engine.hpp"
#include "aurea/export/OfflineAudioSnapshot.hpp"
#include "aurea/project/Project.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstring>

using namespace aurea;
using namespace aurea::plugins;

namespace {
const char* delayManifest = R"({"aureaPlugin":3,"id":"test.audio.delay","version":"1.0.0",
"name":"Delay","kind":"audio","params":[{"id":"time","name":"Time","type":"float","default":10,"min":1,"max":100}],
"audio":{"nodes":[{"id":"out","type":"mix","inputs":["dry","echo"],"gains":[1,0.5]},
{"id":"echo","type":"delay","input":"dry","timeMs":{"param":"time"},"feedback":0.4,"maxDelayMs":100},
{"id":"dry","type":"input"}],"output":"out"}})";
std::shared_ptr<const AudioProgram> delay_program() {
    Manifest manifest; std::string error;
    if (!parse_manifest(delayManifest, manifest, error)) { std::printf("%s\n", error.c_str()); return {}; }
    auto result = prepare_audio_program(manifest.audioGraph, ".");
    return result.ok() ? *result : nullptr;
}
class NoAudio final : public audio::BlockSource {
    const audio::AudioBlock* block(u64, i64) override { return nullptr; }
};
}

AUREA_TEST(AudioPlugin, ManifestRoundtripAndGraphAdmission) {
    Manifest manifest; std::string error;
    AUREA_CHECK(parse_manifest(delayManifest, manifest, error));
    if (!manifest.audioGraph) return;
    AUREA_CHECK(manifest.kind == Kind::Audio);
    AUREA_CHECK(manifest.audioGraph->nodes[0].kind == AudioNodeKind::Input);
    AUREA_CHECK(manifest.audioGraph->nodes.back().kind == AudioNodeKind::Mix);
    Manifest restored;
    AUREA_CHECK(parse_manifest(manifest_json(manifest), restored, error));
    AUREA_CHECK(restored.audioGraph && restored.audioGraph->fingerprint == manifest.audioGraph->fingerprint);
    const auto parseGraph = [&](const char* text) {
        json::Value value; std::shared_ptr<const AudioGraph> graph;
        return json::parse(text, value) && parse_audio_graph(value, manifest.params, graph, error);
    };
    AUREA_CHECK(!parseGraph(R"({"nodes":[{"id":"a","type":"gain","input":"b"},{"id":"b","type":"gain","input":"a"}],"output":"a"})"));
    AUREA_CHECK(!parseGraph(R"({"nodes":[{"id":"a","type":"input"},{"id":"b","type":"gain","input":"missing"}],"output":"b"})"));
    AUREA_CHECK(!parseGraph(R"({"nodes":[{"id":"a","type":"input"},{"id":"b","type":"gain","input":"a","gain":{"param":"missing"}}],"output":"b"})"));
    AUREA_CHECK(!parseGraph(R"({"nodes":[{"id":"a","type":"input"},{"id":"b","type":"delay","input":"a","maxDelayMs":8001}],"output":"b"})"));
    AUREA_CHECK(!parseGraph(R"({"impulses":[{"id":"x","file":"../escape.wav"}],"nodes":[{"id":"a","type":"input"}],"output":"a"})"));
    AUREA_CHECK(!parseGraph(R"({"nodes":[{"id":"a","type":"input","executable":"x.dll"}],"output":"a"})"));
}

AUREA_TEST(AudioPlugin, DelayStateAndBlockPartitionIndependence) {
    auto program = delay_program(); AUREA_CHECK(bool(program)); if (!program) return;
    AudioProcessor whole, chunks;
    AUREA_CHECK(whole.prepare(program).ok()); AUREA_CHECK(chunks.prepare(program).ok());
    std::vector<f32> input(4096 * 2), expected(input.size()), output(input.size());
    input[0] = 1.f; input[1] = -.5f;
    AUREA_CHECK(whole.process_block(input.data(), expected.data(), 4096, 48000, 2, {}).ok());
    for (u32 position = 0; position < 4096;) {
        const u32 count = std::min(4096 - position, 1u + (position * 17u + 37u) % 237u);
        AUREA_CHECK(chunks.process_block(input.data() + position * 2, output.data() + position * 2, count, 48000, 2, {}).ok());
        position += count;
    }
    for (usize i = 0; i < output.size(); ++i) AUREA_CHECK(output[i] == expected[i]);
    AUREA_CHECK_NEAR(output[0], 1, 1e-6);
    AUREA_CHECK_NEAR(output[480 * 2], .5, 1e-6);
    const f64 dampedFeedback = .2 * (1.0 - std::exp(-2.0 * 3.14159265358979323846 * 12000.0 / 48000.0));
    AUREA_CHECK_NEAR(output[960 * 2], dampedFeedback, 1e-6);
    AUREA_CHECK(!whole.process_block(input.data(), output.data(), 1, 44100, 2, {}).ok());
    AUREA_CHECK(!whole.process_block(input.data(), output.data(), 4097, 48000, 2, {}).ok());
    AUREA_CHECK(whole.state_bytes() < kAudioMaxStateBytes);
}

AUREA_TEST(AudioPlugin, PartitionedConvolutionMatchesDirectAtAllBoundaries) {
    constexpr u32 frames = 6000, inputFrames = 333;
    std::vector<f32> impulse(frames * 2), input(inputFrames * 2);
    for (u32 i = 0; i < frames; ++i) {
        impulse[i * 2] = std::sin(i * .71f) * .003f;
        impulse[i * 2 + 1] = std::cos(i * .37f) * .002f;
    }
    for (u32 boundary : {0u,127u,128u,1023u,1024u,4095u,4096u,5999u}) {
        impulse[boundary * 2] += .1f; impulse[boundary * 2 + 1] -= .07f;
    }
    for (u32 i = 0; i < inputFrames; ++i) { input[i * 2] = std::sin(i * .17f); input[i * 2 + 1] = std::cos(i * .11f); }
    auto kernel = audio::prepare_convolution(impulse, 2, kAudioMaxImpulseFrames);
    AUREA_CHECK(kernel.ok()); if (!kernel.ok()) return;
    audio::PartitionedConvolver processor; AUREA_CHECK(processor.prepare(*kernel).ok());
    for (u32 frame = 0; frame < frames + inputFrames + 300; ++frame) {
        f32 left = 0, right = 0;
        processor.process(frame < inputFrames ? input[frame * 2] : 0, frame < inputFrames ? input[frame * 2 + 1] : 0, left, right);
        f64 direct[2]{};
        for (u32 source = 0; source < inputFrames; ++source) {
            if (source > frame || frame - source >= frames) continue;
            for (u32 channel = 0; channel < 2; ++channel) direct[channel] += input[source * 2 + channel] * impulse[(frame - source) * 2 + channel];
        }
        AUREA_CHECK_NEAR(left, direct[0], 2e-5); AUREA_CHECK_NEAR(right, direct[1], 2e-5);
    }
    // Reusing an instance starts with zero history.
    AUREA_CHECK(processor.prepare(*kernel).ok());
    f32 left = 1, right = 1; processor.process(0, 0, left, right);
    AUREA_CHECK_NEAR(left, 0, 1e-7); AUREA_CHECK_NEAR(right, 0, 1e-7);
}

AUREA_TEST(AudioPlugin, MixerRunsPluginAndPreservesStreamingState) {
    auto program = delay_program(); AUREA_CHECK(bool(program)); if (!program) return;
    audio::AudioMixSnapshot snapshot;
    audio::AudioClip clip; clip.start = 0; clip.end = 4096; clip.streamKey = 1; clip.fxHash = 2;
    clip.fadeFrom = 0; clip.fadeTo = 4096;
    audio::FxStage tone; tone.kind = audio::FxKind::Tone; tone.params.resize(audio::fxp::kToneCount);
    tone.params[audio::fxp::kFreq1].value = 440; tone.params[audio::fxp::kToneLevel].value = 50;
    audio::FxStage plugin; plugin.kind = audio::FxKind::Plugin; plugin.plugin = program;
    plugin.params.resize(1); plugin.params[0].value = 10;
    clip.chain = {tone, plugin}; snapshot.clips.push_back(clip);
    NoAudio source; audio::MixState whole, chunks;
    std::vector<f32> expected(8192), output(8192);
    audio::MixStats stats;
    audio::mix(snapshot, 0, 4096, source, expected.data(), &stats, &whole, false);
    for (u32 at = 0; at < 4096;) {
        const u32 count = std::min(4096 - at, 157u);
        audio::mix(snapshot, at, count, source, output.data() + at * 2, &stats, &chunks, false); at += count;
    }
    AUREA_CHECK(stats.dspError == Errc::Ok); AUREA_CHECK(stats.missingBlocks == 0);
    for (usize i = 0; i < output.size(); ++i) AUREA_CHECK(expected[i] == output[i]);
    AUREA_CHECK(stats.peak > .1f);
}

AUREA_TEST(AudioPlugin, ExamplesProcessRealPcmAndReportCpuAndState) {
    for (const char* name : {"audio-delay", "audio-room", "audio-eq", "audio-compressor", "audio-convolution"}) {
        const auto directory = std::filesystem::path(AUREA_PLUGIN_EXAMPLES_DIR) / name;
        std::ifstream file(directory / "plugin.json", std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(file)), {});
        Manifest manifest; std::string error;
        AUREA_CHECK_MSG(parse_manifest(text, manifest, error), error.c_str());
        if (!manifest.audioGraph) continue;
        auto program = prepare_audio_program(manifest.audioGraph, directory.string());
        AUREA_CHECK(program.ok()); if (!program.ok()) continue;
        AudioProcessor processor;
        AUREA_CHECK(processor.prepare(*program).ok());
        std::vector<f32> input(8192), output(8192);
        for (usize i = 0; i < input.size(); ++i) input[i] = std::sin(static_cast<f32>(i) * .03f) * .7f;
        f64 totalMs = 0, maxMs = 0;
        for (u32 block = 0; block < 16; ++block) {
            const auto start = std::chrono::steady_clock::now();
            AUREA_CHECK(processor.process_block(input.data(), output.data(), 4096, 48000, 2, {}).ok());
            const f64 milliseconds = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
            totalMs += milliseconds; maxMs = std::max(maxMs, milliseconds);
            for (f32 sample : output) AUREA_CHECK(std::isfinite(sample));
        }
        AUREA_CHECK(processor.state_bytes() < kAudioMaxStateBytes);
        std::printf("    %s CPU %.3f ms avg / %.3f ms max per 4096 frames; state %llu; IR %llu bytes\n", name,
            totalMs / 16, maxMs, static_cast<unsigned long long>(processor.state_bytes()),
            static_cast<unsigned long long>((*program)->immutableBytes));
    }
}

AUREA_TEST(AudioPlugin, NormalLayerSnapshotAndOfflineExportRetainTheProgram) {
    Engine engine; EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
    config.memoryBudgetBytes = 64ull << 20;
    AUREA_CHECK(engine.initialize(config).ok());
    AUREA_CHECK(engine.new_project(64, 36, 30, "DSP export").ok());
    auto layer = engine.add_shape(0); AUREA_CHECK(layer.ok()); if (!layer.ok()) return;
    Manifest manifest; std::string error;
    AUREA_CHECK(parse_manifest(delayManifest, manifest, error));
    auto program = delay_program(); AUREA_CHECK(bool(program)); if (!program) return;
    AUREA_CHECK(engine.effects().add(make_audio_effect(manifest, program)).ok());
    Command add; add.type = CommandType::EffectAdd; add.effect_add.layer = LayerId::unpack(*layer);
    add.effect_add.effectType = effect_type_id(audio::fx_keys::kTone);
    AUREA_CHECK(engine.apply_command(add).ok());
    const auto type = effect_type_id(effect_key(manifest.id)); add.effect_add.effectType = type;
    AUREA_CHECK(engine.apply_command(add).ok());
    const auto* comp = engine.project()->timeline().composition(engine.project()->timeline().root());
    auto snapshot = audio::build_snapshot(*comp, *engine.project(), nullptr, nullptr, nullptr, nullptr, &engine.effects());
    AUREA_CHECK(snapshot && snapshot->clips.size() == 1);
    if (!snapshot || snapshot->clips.empty()) return;
    AUREA_CHECK(std::any_of(snapshot->clips[0].chain.begin(), snapshot->clips[0].chain.end(),
        [](const auto& stage) { return stage.kind == audio::FxKind::Plugin && stage.plugin; }));
    auto offline = OfflineAudioSnapshot::create(*comp, *engine.project(), nullptr, nullptr, nullptr,
        OfflineAudioSnapshot::kDefaultBlockBudget, nullptr, &engine.effects());
    AUREA_CHECK(offline.ok()); if (!offline.ok()) return;
    AUREA_CHECK(engine.effects().retire(type));
    NoAudio source; audio::MixState previewState, exportState; audio::MixStats stats;
    std::vector<f32> preview(8192), exported(8192);
    audio::mix(*snapshot, 0, 4096, source, preview.data(), &stats, &previewState);
    AUREA_CHECK((*offline)->mix(0, 4096, source, exported.data(), &stats, &exportState).ok());
    AUREA_CHECK(stats.dspError == Errc::Ok);
    for (usize i = 0; i < preview.size(); ++i) AUREA_CHECK(preview[i] == exported[i]);
}

AUREA_TEST(AudioPlugin, OneThousandParametersReachTheEffectRegistry) {
    std::string text = R"({"aureaPlugin":3,"id":"test.audio.parameters","version":"1.0.0","name":"Parameters","kind":"audio","params":[)";
    for (u32 i = 0; i < kMaxParams; ++i) {
        if (i) text += ',';
        text += "{\"id\":\"control_" + std::to_string(i) + "\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.5}";
    }
    text += R"(],"audio":{"nodes":[{"id":"source","type":"input"}],"output":"source"}})";
    Manifest manifest; std::string error;
    AUREA_CHECK(parse_manifest(text, manifest, error));
    AUREA_CHECK_EQ(manifest.params.size(), 1024u);
    auto program = prepare_audio_program(manifest.audioGraph, ".");
    AUREA_CHECK(program.ok()); if (!program.ok()) return;
    EffectRegistry registry;
    AUREA_CHECK(registry.add(make_audio_effect(manifest, *program)).ok());
    const auto* parameters = registry.params(effect_type_id(effect_key(manifest.id)));
    AUREA_CHECK(parameters && parameters->count() == 1024);
    Manifest restored;
    AUREA_CHECK(parse_manifest(manifest_json(manifest), restored, error));
    AUREA_CHECK_EQ(restored.params.size(), 1024u);
}

AUREA_TEST(AudioPlugin, FullLengthConvolutionWrapsItsSpectralHistoryCorrectly) {
    constexpr u32 frames = kAudioMaxImpulseFrames;
    std::vector<f32> impulse(frames);
    for (u32 tap : {0u, 127u, 128u, 1023u, 1024u, 4095u, 4096u, 16383u, 65535u, frames - 1})
        impulse[tap] = .07f + (tap % 7) * .01f;
    auto kernel = audio::prepare_convolution(impulse, 1, frames);
    AUREA_CHECK(kernel.ok()); if (!kernel.ok()) return;
    audio::PartitionedConvolver processor; AUREA_CHECK(processor.prepare(*kernel).ok());
    const u32 starts[] = {0, 129, 4095};
    for (u32 frame = 0; frame < frames + 4200; ++frame) {
        f32 input = 0, expected = 0;
        for (u32 start : starts) {
            if (frame == start) input += 1;
            if (frame >= start && frame - start < frames) expected += impulse[frame - start];
        }
        f32 left = 0, right = 0;
        processor.process(input, -input, left, right);
        AUREA_CHECK_NEAR(left, expected, 2e-5); AUREA_CHECK_NEAR(right, -expected, 2e-5);
    }
}

AUREA_TEST(AudioPlugin, WavFormatsAndNonfiniteImpulseAdmission) {
    namespace fs = std::filesystem;
    const auto directory = fs::temp_directory_path() / ("aurea-audio-ir-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory);
    json::Value value; std::string error; std::shared_ptr<const AudioGraph> graph;
    AUREA_CHECK(json::parse(R"({"impulses":[{"id":"room","file":"ir.wav"}],"nodes":[{"id":"in","type":"input"},
        {"id":"out","type":"convolution","input":"in","impulse":"room"}],"output":"out"})", value));
    AUREA_CHECK(parse_audio_graph(value, {}, graph, error));
    struct Format { u16 kind, bits; u32 raw; bool extensible; };
    for (const Format format : {Format{1,16,0x4000,false}, Format{1,24,0x400000,false},
        Format{1,32,0x40000000,false}, Format{3,32,0x3f000000,false}, Format{3,32,0x3f000000,true},
        Format{3,32,0x7fc00000,false}}) {
        const u32 fmtSize = format.extensible ? 40 : 16;
        const u32 sampleBytes = format.bits / 8, dataAt = 20 + fmtSize;
        std::vector<u8> bytes(dataAt + 8 + sampleBytes + (sampleBytes & 1));
        auto put16 = [&](u32 at, u16 number) { bytes[at] = number & 255; bytes[at+1] = number >> 8; };
        auto put32 = [&](u32 at, u32 number) { for (u32 i=0;i<4;++i) bytes[at+i] = static_cast<u8>(number >> (8*i)); };
        std::memcpy(bytes.data(), "RIFF", 4); put32(4, static_cast<u32>(bytes.size()-8));
        std::memcpy(bytes.data()+8, "WAVEfmt ", 8); put32(16, fmtSize);
        put16(20, format.extensible ? 0xfffe : format.kind); put16(22, 1); put32(24, 48000);
        put32(28, 48000 * sampleBytes); put16(32, static_cast<u16>(sampleBytes)); put16(34, format.bits);
        if (format.extensible) {
            put16(36, 22); put16(38, format.bits); put32(40, 4); put16(44, format.kind);
            const u8 suffix[] = {0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
            std::memcpy(bytes.data()+46, suffix, sizeof(suffix));
        }
        std::memcpy(bytes.data()+dataAt, "data", 4); put32(dataAt+4, sampleBytes);
        for (u32 i=0;i<sampleBytes;++i) bytes[dataAt+8+i] = static_cast<u8>(format.raw >> (8*i));
        { std::ofstream file(directory/"ir.wav", std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
        auto program = prepare_audio_program(graph, directory.string());
        if (format.raw == 0x7fc00000) { AUREA_CHECK(!program.ok()); continue; }
        AUREA_CHECK(program.ok()); if (!program.ok()) continue;
        AudioProcessor processor; AUREA_CHECK(processor.prepare(*program).ok());
        f32 input[2] = {1,-1}, output[2]{};
        AUREA_CHECK(processor.process_block(input, output, 1, 48000, 2, {}).ok());
        AUREA_CHECK_NEAR(output[0], .5, 1e-6); AUREA_CHECK_NEAR(output[1], -.5, 1e-6);
    }
    fs::remove(directory / "ir.wav"); fs::remove(directory);
}
