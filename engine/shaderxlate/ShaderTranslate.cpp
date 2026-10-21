// =============================================================================
//  Aurea / shaderxlate / ShaderTranslate.cpp
//
//  A lógica que antes vivia em tools/gles-shaders/main.cpp e
//  tools/metal-shaders/main.cpp, sem mudar uma vírgula do resultado: as duas
//  ferramentas agora são cascas que leem o .spv, chamam estas funções e gravam
//  o arquivo. Os backends GLES e Metal chamam as mesmas funções em runtime.
//
//  Este é o ÚNICO arquivo do motor compilado com exceções (o SPIRV-Cross as
//  usa para erro). Nada atravessa a fronteira: toda função pública é noexcept
//  e devolve o erro em texto.
// =============================================================================
#include "ShaderTranslate.hpp"

#include "spirv_glsl.hpp"
#if defined(AUREA_XLATE_MSL)
#include "spirv_msl.hpp"
#endif

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <vector>

namespace aurea::xlate {

namespace {

std::vector<uint32_t> to_vector(const uint32_t* words, size_t count) {
    if (!words || count < 5) throw std::runtime_error("invalid SPIR-V file");
    if (words[0] != 0x07230203u) throw std::runtime_error("invalid SPIR-V magic");
    return std::vector<uint32_t>(words, words + count);
}

} // namespace

uint64_t spirv_fingerprint(const uint32_t* words, size_t wordCount) noexcept {
    uint64_t fingerprint = 14695981039346656037ull;
    const auto* bytes = reinterpret_cast<const unsigned char*>(words);
    for (size_t i = 0; words && i < wordCount * 4; ++i) {
        fingerprint ^= bytes[i];
        fingerprint *= 1099511628211ull;
    }
    return fingerprint;
}

Stage spirv_stage(const uint32_t* words, size_t wordCount) noexcept {
    try {
        spirv_cross::Compiler compiler(to_vector(words, wordCount));
        switch (compiler.get_execution_model()) {
            case spv::ExecutionModelVertex: return Stage::Vertex;
            case spv::ExecutionModelFragment: return Stage::Fragment;
            case spv::ExecutionModelGLCompute: return Stage::Compute;
            default: return Stage::Unknown;
        }
    } catch (...) {
        return Stage::Unknown;
    }
}

// -----------------------------------------------------------------------------
// GLES (ESSL 3.10)
// -----------------------------------------------------------------------------
bool spirv_to_essl(const uint32_t* wordsIn, size_t wordCount, std::string& out, std::string& error,
                   const uint32_t* keyWords, size_t keyWordCount) noexcept {
    out.clear();
    error.clear();
    try {
        auto words = to_vector(wordsIn, wordCount);
        // A impressão digital é a do SPIR-V otimizado que o motor embute; com
        // `keyWords`, a do módulo equivalente que o motor procura.
        const uint64_t fingerprint = keyWords ? spirv_fingerprint(keyWords, keyWordCount)
                                              : spirv_fingerprint(words.data(), words.size());
        spirv_cross::CompilerGLSL compiler(std::move(words));
        const auto stage = compiler.get_execution_model();
        const std::string suffix = stage == spv::ExecutionModelVertex ? "vs"
                                 : stage == spv::ExecutionModelFragment ? "fs" : "cs";
        auto options = compiler.get_common_options();
        options.es = true;
        options.version = 310;
        options.emit_push_constant_as_uniform_buffer = true;
        options.vertex.fixup_clipspace = true; // Vulkan [0,1] -> GL [-1,1] depth.
        // Offscreen row zero represents the same logical top row as Vulkan.
        // Only presentation to an EGL window flips vertically; uploads,
        // gl_FragCoord, texture sampling and readback stay in that convention.
        options.vertex.flip_vert_y = false;
        options.fragment.default_float_precision = spirv_cross::CompilerGLSL::Options::Highp;
        options.fragment.default_int_precision = spirv_cross::CompilerGLSL::Options::Highp;
        compiler.set_common_options(options);
        const auto resources = compiler.get_shader_resources();
        auto remap = [&](const auto& list, uint32_t first, const char* kind) {
            for (const auto& resource : list) {
                const uint32_t slot = compiler.get_decoration(resource.id, spv::DecorationBinding);
                if (slot < first) throw std::runtime_error("unexpected Aurea binding");
                const uint32_t mapped = slot - first;
                compiler.unset_decoration(resource.id, spv::DecorationDescriptorSet);
                compiler.set_decoration(resource.id, spv::DecorationBinding, mapped);
                compiler.set_name(resource.id, std::string("aurea_") + kind + "_" + suffix + std::to_string(mapped));
                if (std::string(kind) == "ubo" || std::string(kind) == "ssbo")
                    compiler.set_name(resource.base_type_id, std::string("Aurea_") + kind + "_" + suffix + std::to_string(mapped));
            }
        };
        remap(resources.sampled_images, 0, "sampler");
        remap(resources.uniform_buffers, 12, "ubo");
        remap(resources.storage_images, 13, "image");
        remap(resources.storage_buffers, 15, "ssbo");
        for (const auto& resource : resources.push_constant_buffers) {
            compiler.set_decoration(resource.id, spv::DecorationBinding, 1);
            compiler.set_name(resource.base_type_id, "Aurea_push_" + suffix);
            compiler.set_name(resource.id, "aurea_push_" + suffix);
        }
        // Monolithic ES programs link stage interfaces by name. SPIR-V IDs
        // are module-local and therefore cannot serve as matching names.
        const auto& varying = stage == spv::ExecutionModelVertex ? resources.stage_outputs : resources.stage_inputs;
        if (stage != spv::ExecutionModelGLCompute) for (const auto& resource : varying) {
            const auto location = compiler.get_decoration(resource.id, spv::DecorationLocation);
            compiler.set_name(resource.id, "aurea_varying_" + std::to_string(location));
        }
        auto source = compiler.compile();
        std::string helpers;
        for (const auto& resource : resources.sampled_images) {
            const auto& type = compiler.get_type(resource.type_id);
            if (type.image.dim != spv::Dim2D || type.image.arrayed || type.image.ms || type.image.depth) continue;
            const auto slot = compiler.get_decoration(resource.id, spv::DecorationBinding);
            const auto tag = suffix + std::to_string(slot);
            const auto sampler = "aurea_sampler_" + tag;
            const auto uniform = "aurea_border_" + tag;
            const auto function = "aurea_sample_" + tag;
            const auto needle = "texture(" + sampler + ",";
            const auto replacement = function + "(" + sampler + ",";
            size_t position = 0;
            while ((position = source.find(needle, position)) != std::string::npos) {
                source.replace(position, needle.size(), replacement); position += replacement.size();
            }
            // Core ES has clamp-to-edge, but the editor needs transparent
            // borders. For its single-level effect targets, edge sampling
            // times bilinear border coverage is exactly clamp-to-border.
            helpers += "uniform highp ivec2 " + uniform + ";\n";
            helpers += "highp vec4 " + function + "(highp sampler2D tex, highp vec2 uv) {\n"
                "highp vec4 value = texture(tex, uv);\n"
                "if (" + uniform + ".x == 0) return value;\n"
                "highp vec2 weight;\n"
                "if (" + uniform + ".y != 0) weight = vec2(uv.x >= 0.0 && uv.x < 1.0 ? 1.0 : 0.0, uv.y >= 0.0 && uv.y < 1.0 ? 1.0 : 0.0);\n"
                "else { highp vec2 size = vec2(textureSize(tex, 0));\n"
                "weight = clamp(uv * size + 0.5, 0.0, 1.0) * clamp((1.0 - uv) * size + 0.5, 0.0, 1.0); }\n"
                "return value * ((" + uniform + ".x & 1) != 0 ? weight.x : 1.0) * ((" + uniform + ".x & 2) != 0 ? weight.y : 1.0);\n}\n";
        }
        if (!helpers.empty()) {
            const auto extension = source.rfind("#extension");
            const auto headerEnd = source.find('\n', extension == std::string::npos ? 0 : extension);
            source.insert(headerEnd + 1, "\n" + helpers);
        }
        char header[64];
        std::snprintf(header, sizeof(header), "// AUREA_SPIRV_FNV64 %llx\n", static_cast<unsigned long long>(fingerprint));
        out.reserve(source.size() + 40);
        out = header;
        out += source;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
    } catch (...) {
        error = "falha desconhecida na traducao para ESSL";
    }
    out.clear();
    return false;
}

// -----------------------------------------------------------------------------
// Metal (MSL)
// -----------------------------------------------------------------------------
const char* msl_entry_for(Stage stage) noexcept {
    return stage == Stage::Vertex ? "vs_main" : stage == Stage::Fragment ? "fs_main" : "cs_main";
}

#if defined(AUREA_XLATE_MSL)

bool msl_available() noexcept { return true; }

bool spirv_to_msl(const uint32_t* wordsIn, size_t wordCount, const char* entry, uint32_t fragOutputMask,
                  uint32_t group[3], std::string& out, std::string& error) noexcept {
    out.clear();
    error.clear();
    try {
        spirv_cross::CompilerMSL compiler(to_vector(wordsIn, wordCount));
        const auto stage = compiler.get_execution_model();
        compiler.rename_entry_point("main", entry, stage);
        auto common = compiler.get_common_options();
        // The engine's projection and full-screen triangle use Vulkan NDC:
        // y=-1 is the top edge. Metal's viewport places y=+1 at the top.
        common.vertex.flip_vert_y = true;
        compiler.set_common_options(common);
        spirv_cross::CompilerMSL::Options options;
        options.platform = spirv_cross::CompilerMSL::Options::iOS;
        options.set_msl_version(2, 1);
        options.enable_frag_output_mask = fragOutputMask;
        compiler.set_msl_options(options);
        for (uint32_t slot = 0; slot <= 16; ++slot) {
            spirv_cross::MSLResourceBinding binding;
            binding.stage = stage;
            binding.desc_set = 0;
            binding.binding = slot;
            binding.msl_buffer = binding.msl_texture = binding.msl_sampler = slot;
            compiler.add_msl_resource_binding(binding);
        }
        spirv_cross::MSLResourceBinding push;
        push.stage = stage;
        push.desc_set = spirv_cross::ResourceBindingPushConstantDescriptorSet;
        push.binding = spirv_cross::ResourceBindingPushConstantBinding;
        push.msl_buffer = 30;
        compiler.add_msl_resource_binding(push);
        if (group && stage == spv::ExecutionModelGLCompute) {
            for (unsigned i = 0; i < 3; ++i)
                group[i] = compiler.get_execution_mode_argument(spv::ExecutionModeLocalSize, i);
            if (!group[0] || !group[1] || !group[2]) throw std::runtime_error("missing compute group size");
        }
        out = compiler.compile();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
    } catch (...) {
        error = "falha desconhecida na traducao para MSL";
    }
    out.clear();
    return false;
}

bool spirv_writes_color1_or_more(const uint32_t* wordsIn, size_t wordCount) noexcept {
    try {
        spirv_cross::CompilerMSL compiler(to_vector(wordsIn, wordCount));
        if (compiler.get_execution_model() != spv::ExecutionModelFragment) return false;
        for (const auto& output : compiler.get_shader_resources().stage_outputs) {
            if (compiler.has_decoration(output.id, spv::DecorationLocation)
                && compiler.get_decoration(output.id, spv::DecorationLocation) >= 1) return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}

#else

bool msl_available() noexcept { return false; }

bool spirv_to_msl(const uint32_t*, size_t, const char*, uint32_t, uint32_t*, std::string& out,
                  std::string& error) noexcept {
    out.clear();
    error = "traducao para MSL ausente neste binario";
    return false;
}

bool spirv_writes_color1_or_more(const uint32_t*, size_t) noexcept { return false; }

#endif

} // namespace aurea::xlate
