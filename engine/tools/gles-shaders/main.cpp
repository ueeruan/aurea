// Host tool: the same optimized SPIR-V used by Vulkan/Metal becomes ESSL 3.10.
// Binding remapping is confined to this backend's ABI, never effect sources.
#include "spirv_glsl.hpp"
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try {
        std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
        const auto size = input.tellg();
        if (size <= 0 || size % 4) throw std::runtime_error("invalid SPIR-V file");
        std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(words.data()), size)) throw std::runtime_error("SPIR-V read failed");
        uint64_t fingerprint = 14695981039346656037ull;
        for (size_t i = 0; i < words.size() * 4; ++i) {
            fingerprint ^= reinterpret_cast<const unsigned char*>(words.data())[i];
            fingerprint *= 1099511628211ull;
        }
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
        std::ofstream output(argv[2], std::ios::binary);
        output << "// AUREA_SPIRV_FNV64 " << std::hex << fingerprint << '\n';
        output.write(source.data(), source.size());
        if (!output) throw std::runtime_error("ESSL write failed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << argv[1] << ": " << error.what() << '\n';
        return 1;
    }
}
