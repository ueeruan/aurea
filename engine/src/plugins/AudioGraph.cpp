#include "aurea/plugins/AudioDsp.hpp"
#include "aurea/audio/Audio.hpp"
#include "aurea/plugins/Plugin.hpp"
#include "aurea/project/Presets.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>

namespace aurea::plugins {
namespace {
using json::Value;
u64 hash_bytes(u64 hash, const void* data, usize size) {
    const auto* bytes = static_cast<const u8*>(data);
    for (usize i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}
void write_value(json::Writer& writer, const Value& value) {
    switch (value.type) {
        case Value::Type::Null: writer.null(); break;
        case Value::Type::Bool: writer.value(value.boolean); break;
        case Value::Type::Number: writer.value(value.number); break;
        case Value::Type::String: writer.value(value.string); break;
        case Value::Type::Array:
            writer.begin_array();
            for (const auto& item : value.array) write_value(writer, item);
            writer.end_array(); break;
        case Value::Type::Object:
            writer.begin_object();
            for (const auto& [key, item] : value.object) { writer.key(key); write_value(writer, item); }
            writer.end_object(); break;
    }
}
bool relative_file(const std::string& file) {
    if (file.empty() || file.size() > 240 || file.front() == '/' || file.front() == '\\'
        || file.find(':') != std::string::npos || file.find('\\') != std::string::npos) return false;
    for (const auto& part : std::filesystem::u8path(file))
        if (part == ".." || part == ".") return false;
    return true;
}
bool scalar_kind(ParamKind kind) {
    return kind == ParamKind::Float || kind == ParamKind::Int || kind == ParamKind::Angle
        || kind == ParamKind::Bool || kind == ParamKind::Enum;
}
u16 le16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
u32 le32(const u8* p) { return p[0] | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }
Result<std::shared_ptr<const audio::ConvolutionKernel>> load_impulse(
    const std::filesystem::path& root, const std::string& file, u64& fingerprint) {
    std::error_code ec;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec) return Status{Errc::IoError};
    const auto path = std::filesystem::canonical(root / std::filesystem::u8path(file), ec);
    if (ec) return Status{Errc::IoError, "audio impulse file is missing"};
    auto relative = std::filesystem::relative(path, canonicalRoot, ec);
    if (ec || relative.empty() || relative.is_absolute()) return Status{Errc::InvalidArgument};
    for (const auto& part : relative) if (part == ".." || part == ".") return Status{Errc::InvalidArgument};
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size < 44 || size > kAudioMaxImpulseFileBytes) return Status{Errc::BudgetExceeded};
    std::vector<u8> bytes(static_cast<usize>(size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) return Status{Errc::IoError};
    if (std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4)
        || u64(le32(bytes.data() + 4)) + 8 > bytes.size()) return Status{Errc::CorruptData};
    const u64 end = u64(le32(bytes.data() + 4)) + 8;
    u16 format = 0, channels = 0, bits = 0, alignment = 0;
    u32 rate = 0;
    const u8* samples = nullptr;
    usize sampleBytes = 0;
    for (u64 position = 12; position + 8 <= end;) {
        const auto* chunk = bytes.data() + position;
        const u32 length = le32(chunk + 4);
        if (u64(length) > end - position - 8) return Status{Errc::CorruptData};
        if (!std::memcmp(chunk, "fmt ", 4)) {
            if (length < 16) return Status{Errc::CorruptData};
            format = le16(chunk + 8); channels = le16(chunk + 10);
            rate = le32(chunk + 12); alignment = le16(chunk + 20); bits = le16(chunk + 22);
            if (format == 0xfffe) {
                static constexpr u8 suffix[14] = {0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if (length < 40 || le16(chunk + 24) < 22 || std::memcmp(chunk + 34, suffix, sizeof(suffix)))
                    return Status{Errc::CorruptData};
                format = le16(chunk + 32);
            }
        } else if (!std::memcmp(chunk, "data", 4)) { samples = chunk + 8; sampleBytes = length; }
        position += 8ull + length + (length & 1u);
    }
    if (!samples || !sampleBytes || channels < 1 || channels > 2 || rate < 8000 || rate > 192000
        || !((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32))
        || alignment != channels * (bits / 8) || sampleBytes % alignment) return Status{Errc::CorruptData};
    const u64 sourceFrames = sampleBytes / alignment;
    const u64 frames = (sourceFrames * kAudioSampleRate + rate - 1) / rate;
    if (!frames || frames > kAudioMaxImpulseFrames) return Status{Errc::BudgetExceeded};
    std::vector<f32> decoded(static_cast<usize>(sourceFrames) * channels);
    for (usize i = 0; i < decoded.size(); ++i) {
        const u8* p = samples + i * (bits / 8);
        if (format == 3) { const u32 raw = le32(p); std::memcpy(&decoded[i], &raw, 4); }
        else if (bits == 16) decoded[i] = static_cast<i16>(le16(p)) / 32768.f;
        else if (bits == 24) {
            i32 raw = i32(p[0]) | (i32(p[1]) << 8) | (i32(p[2]) << 16);
            if (raw & 0x800000) raw |= static_cast<i32>(0xff000000u);
            decoded[i] = raw / 8388608.f;
        } else decoded[i] = static_cast<i32>(le32(p)) / 2147483648.f;
        if (!std::isfinite(decoded[i]) || std::fabs(decoded[i]) > 16.f) return Status{Errc::CorruptData};
    }
    std::vector<f32> impulse;
    if (rate == kAudioSampleRate) impulse = std::move(decoded);
    else {
        std::vector<f32> stereo(sourceFrames * 2);
        for (u64 frame = 0; frame < sourceFrames; ++frame) {
            stereo[frame * 2] = decoded[frame * channels];
            stereo[frame * 2 + 1] = decoded[frame * channels + channels - 1];
        }
        impulse.resize(frames * 2);
        audio::resample_to_mix(stereo.data(), sourceFrames, 0, f64(rate) / kAudioSampleRate,
            impulse.data(), static_cast<u32>(frames));
        channels = 2;
    }
    fingerprint = hash_bytes(fingerprint, bytes.data(), bytes.size());
    return audio::prepare_convolution(impulse, channels, kAudioMaxImpulseFrames);
}
}

bool parse_audio_graph(const Value& value, const std::vector<ParamDecl>& params,
    std::shared_ptr<const AudioGraph>& output, std::string& error) {
    output.reset();
    auto fail = [&](std::string message) { error = "audio: " + message; return false; };
    if (!value.is_object()) return fail("expected a graph object");
    auto graph = std::make_shared<AudioGraph>();
    std::unordered_map<std::string, u32> parameterIds, nodeIds, impulseIds;
    for (u32 i = 0; i < params.size(); ++i) {
        if (!scalar_kind(params[i].kind)) return fail("DSP parameters must be scalar");
        parameterIds.emplace(params[i].id, i);
        graph->defaults.push_back(params[i].def[0]);
        graph->minima.push_back(params[i].min); graph->maxima.push_back(params[i].max);
    }
    if (const auto* impulses = value.get("impulses")) {
        if (!impulses->is_array() || impulses->array.size() > 1) return fail("at most one impulse is admitted");
        for (const auto& item : impulses->array) {
            const auto* id = item.get("id"); const auto* file = item.get("file");
            if (!id || !id->is_string() || id->string.empty() || !file || !file->is_string()
                || !relative_file(file->string) || !impulseIds.emplace(id->string, u32(graph->impulses.size())).second)
                return fail("invalid impulse id or package-relative file");
            graph->impulses.push_back({id->string, file->string});
        }
    }
    const auto* nodes = value.get("nodes"); const auto* outputId = value.get("output");
    if (!nodes || !nodes->is_array() || nodes->array.empty() || nodes->array.size() > kAudioMaxNodes
        || !outputId || !outputId->is_string()) return fail("graph needs 1..64 nodes and an output id");
    struct NodeType { const char* name; AudioNodeKind kind; std::vector<std::pair<const char*, f32>> controls; };
    const std::vector<NodeType> types = {
        {"input", AudioNodeKind::Input, {}}, {"gain", AudioNodeKind::Gain, {{"gain", 1}}},
        {"mix", AudioNodeKind::Mix, {}},
        {"delay", AudioNodeKind::Delay, {{"timeMs", 250}, {"feedback", .3f}, {"dampingHz", 12000}}},
        {"allpass", AudioNodeKind::Allpass, {{"timeMs", 15}, {"feedback", .5f}}},
        {"lowpass", AudioNodeKind::Lowpass, {{"frequency", 12000}}},
        {"highpass", AudioNodeKind::Highpass, {{"frequency", 80}}},
        {"eq", AudioNodeKind::Equalizer, {{"frequency", 1000}, {"q", 1}, {"gainDb", 0}}},
        {"compressor", AudioNodeKind::Compressor, {{"thresholdDb", -18}, {"ratio", 4}, {"attackMs", 10},
            {"releaseMs", 100}, {"kneeDb", 6}, {"makeupDb", 0}}},
        {"limiter", AudioNodeKind::Limiter, {{"thresholdDb", -1}, {"ratio", 100}, {"attackMs", 0},
            {"releaseMs", 80}, {"kneeDb", 0}, {"makeupDb", 0}}},
        {"convolution", AudioNodeKind::Convolution, {{"mix", 1}}},
    };
    auto control = [&](const Value* field, f32 fallback, AudioControl& result) {
        result.value = fallback;
        if (!field) return true;
        if (field->is_number()) {
            if (!std::isfinite(field->number) || std::fabs(field->number) > 1e6) return false;
            result.value = static_cast<f32>(field->number); return true;
        }
        const auto* parameter = field->get("param");
        if (!field->is_object() || !parameter || !parameter->is_string()) return false;
        const auto found = parameterIds.find(parameter->string);
        if (found == parameterIds.end()) return false;
        result.parameter = static_cast<i32>(found->second);
        for (const auto& [key, item] : field->object) {
            if (key == "param") continue;
            if ((key != "scale" && key != "offset") || !item.is_number() || !std::isfinite(item.number)
                || std::fabs(item.number) > 1e6) return false;
            (key == "scale" ? result.scale : result.offset) = static_cast<f32>(item.number);
        }
        return true;
    };
    u32 convolutions = 0;
    for (const auto& item : nodes->array) {
        const auto* id = item.get("id"); const auto* type = item.get("type");
        if (!item.is_object() || !id || !id->is_string() || id->string.empty() || id->string.size() > 64
            || !type || !type->is_string() || !nodeIds.emplace(id->string, u32(graph->nodes.size())).second)
            return fail("invalid or duplicate node id/type");
        const auto definition = std::find_if(types.begin(), types.end(), [&](const auto& entry) { return type->string == entry.name; });
        if (definition == types.end()) return fail("unknown node type " + type->string);
        AudioNode node; node.id = id->string; node.kind = definition->kind;
        for (const auto& [key, field] : item.object) {
            const bool common = key == "id" || key == "type" || key == "input" || key == "inputs";
            const bool specific = (key == "gains" && node.kind == AudioNodeKind::Mix)
                || (key == "maxDelayMs" && (node.kind == AudioNodeKind::Delay || node.kind == AudioNodeKind::Allpass))
                || (key == "impulse" && node.kind == AudioNodeKind::Convolution);
            const bool knownControl = std::any_of(definition->controls.begin(), definition->controls.end(),
                [&](const auto& entry) { return key == entry.first; });
            if (!common && !specific && !knownControl) return fail("unknown field " + key + " in " + node.id);
        }
        for (u32 i = 0; i < definition->controls.size(); ++i)
            if (!control(item.get(definition->controls[i].first), definition->controls[i].second, node.controls[i]))
                return fail("invalid control in " + node.id);
        if (node.kind == AudioNodeKind::Delay || node.kind == AudioNodeKind::Allpass) {
            const auto* limit = item.get("maxDelayMs");
            if (!limit || !limit->is_number() || !std::isfinite(limit->number) || limit->number < 1
                || limit->number > kAudioMaxDelayMs || std::floor(limit->number) != limit->number)
                return fail("delay needs integer maxDelayMs in 1..8000");
            node.delayLimitMs = static_cast<u32>(limit->number);
        }
        if (node.kind == AudioNodeKind::Convolution) {
            const auto* impulse = item.get("impulse");
            if (++convolutions > 1 || !impulse || !impulse->is_string() || !impulseIds.count(impulse->string))
                return fail("convolution needs one declared impulse");
            node.impulse = static_cast<i32>(impulseIds.at(impulse->string));
        }
        graph->nodes.push_back(std::move(node));
    }
    for (u32 i = 0; i < graph->nodes.size(); ++i) {
        const auto& item = nodes->array[i]; auto& node = graph->nodes[i];
        std::vector<std::string> references;
        if (const auto* input = item.get("input")) {
            if (!input->is_string() || item.get("inputs")) return fail("use input or inputs");
            references.push_back(input->string);
        }
        if (const auto* inputs = item.get("inputs")) {
            if (!inputs->is_array()) return fail("inputs must be an array");
            for (const auto& input : inputs->array) {
                if (!input.is_string()) return fail("input must name a node");
                references.push_back(input.string);
            }
        }
        if ((node.kind == AudioNodeKind::Input && !references.empty())
            || (node.kind == AudioNodeKind::Mix && (references.empty() || references.size() > kAudioMaxInputs))
            || (node.kind != AudioNodeKind::Mix && node.kind != AudioNodeKind::Input && references.size() != 1))
            return fail("invalid input count in " + node.id);
        for (const auto& reference : references) {
            if (!nodeIds.count(reference)) return fail("missing node " + reference);
            node.inputs.push_back(nodeIds.at(reference));
        }
        if (node.kind == AudioNodeKind::Mix) {
            const auto* gains = item.get("gains");
            if (gains && (!gains->is_array() || gains->array.size() != node.inputs.size())) return fail("mix gains must match inputs");
            for (u32 j = 0; j < node.inputs.size(); ++j)
                if (!control(gains ? &gains->array[j] : nullptr, 1.f, node.controls[j])) return fail("invalid mix gain");
        }
    }
    if (!nodeIds.count(outputId->string)) return fail("output node is missing");
    const u32 originalOutput = nodeIds.at(outputId->string);
    std::vector<u8> visit(graph->nodes.size(), 0);
    std::vector<u32> order;
    std::function<bool(u32)> sort = [&](u32 id) {
        if (visit[id] == 1) return false;
        if (visit[id] == 2) return true;
        visit[id] = 1;
        for (u32 source : graph->nodes[id].inputs) if (!sort(source)) return false;
        visit[id] = 2; order.push_back(id); return true;
    };
    // Validate even unreachable nodes, then eliminate them from execution.
    for (u32 i = 0; i < graph->nodes.size(); ++i) if (!sort(i)) return fail("cycles must use bounded delay/allpass feedback controls");
    std::fill(visit.begin(), visit.end(), 0); order.clear(); sort(originalOutput);
    std::vector<u32> remap(graph->nodes.size());
    for (u32 i = 0; i < order.size(); ++i) remap[order[i]] = i;
    std::vector<AudioNode> sorted;
    for (u32 id : order) {
        auto node = std::move(graph->nodes[id]);
        for (u32& source : node.inputs) source = remap[source];
        sorted.push_back(std::move(node));
    }
    graph->nodes = std::move(sorted); graph->output = remap[originalOutput];
    graph->source = std::make_shared<Value>(value);
    json::Writer writer; write_value(writer, value);
    const auto text = writer.str();
    graph->fingerprint = hash_bytes(1469598103934665603ull, text.data(), text.size());
    for (const auto& parameter : params) {
        graph->fingerprint = hash_bytes(graph->fingerprint, parameter.id.data(), parameter.id.size());
        graph->fingerprint = hash_bytes(graph->fingerprint, parameter.def, sizeof(parameter.def));
        graph->fingerprint = hash_bytes(graph->fingerprint, &parameter.min, sizeof(parameter.min));
        graph->fingerprint = hash_bytes(graph->fingerprint, &parameter.max, sizeof(parameter.max));
    }
    output = std::move(graph); return true;
}

void write_audio_graph(json::Writer& writer, const AudioGraph& graph) { write_value(writer, *graph.source); }

Result<std::shared_ptr<const AudioProgram>> prepare_audio_program(
    std::shared_ptr<const AudioGraph> graph, const std::string& directory) noexcept {
    if (!graph || graph->nodes.empty()) return Status{Errc::InvalidArgument};
    auto program = std::make_shared<AudioProgram>();
    program->graph = std::move(graph); program->fingerprint = program->graph->fingerprint;
    for (const auto& impulse : program->graph->impulses) {
        auto kernel = load_impulse(std::filesystem::u8path(directory), impulse.file, program->fingerprint);
        if (!kernel.ok()) return kernel.status();
        program->immutableBytes += (*kernel)->immutableBytes;
        program->prerollFrames = std::max(program->prerollFrames, (*kernel)->frames);
        program->impulses.push_back(std::move(*kernel));
    }
    program->stateBytes = u64(program->graph->nodes.size()) * kAudioMaxBlockFrames * kAudioChannels * sizeof(f32);
    program->prerollFrames = std::max(program->prerollFrames, kAudioSampleRate / 4);
    for (const auto& node : program->graph->nodes) {
        if (node.kind == AudioNodeKind::Delay || node.kind == AudioNodeKind::Allpass) {
            program->stateBytes += u64(node.delayLimitMs * 48 + 4) * kAudioChannels * sizeof(f32);
            program->prerollFrames = kAudioMaxDelayMs * 48;
        }
        if (node.kind == AudioNodeKind::Convolution) program->stateBytes += program->impulses[node.impulse]->instanceBytes;
    }
    if (program->stateBytes + program->immutableBytes > kAudioMaxStateBytes) return Status{Errc::BudgetExceeded};
    return std::shared_ptr<const AudioProgram>(std::move(program));
}
}
