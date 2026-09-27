// Host tool: preserve the GPUBackend binding ABI when translating to Metal.
#include "spirv_msl.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

/// Entry point the backend looks for per stage (msl_glue.md §2).
const char* entry_for(spv::ExecutionModel stage) {
    return stage == spv::ExecutionModelVertex ? "vs_main"
         : stage == spv::ExecutionModelFragment ? "fs_main" : "cs_main";
}

/// One SPIRV-Cross translation with the engine's fixed resource table.
/// `fragOutputMask`: bit N keeps fragment output location N (msl_glue.md §5).
std::string translate(const std::vector<uint32_t>& words, const char* entry, uint32_t fragOutputMask,
                      uint32_t group[3]) {
    spirv_cross::CompilerMSL compiler(words);
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
    return compiler.compile();
}

/// Does a fragment shader declare an output at `layout(location = N)` with
/// N >= 1 (the second target of the engine's MRT passes)?
bool writes_color1_or_more(const std::vector<uint32_t>& words) {
    spirv_cross::CompilerMSL compiler(words);
    if (compiler.get_execution_model() != spv::ExecutionModelFragment) return false;
    for (const auto& out : compiler.get_shader_resources().stage_outputs) {
        if (compiler.has_decoration(out.id, spv::DecorationLocation)
            && compiler.get_decoration(out.id, spv::DecorationLocation) >= 1) return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try {
        std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
        const auto size = input.tellg();
        if (size <= 0 || size % 4 != 0) throw std::runtime_error("invalid SPIR-V file");
        std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
        input.seekg(0);
        input.read(reinterpret_cast<char*>(words.data()), size);
        const auto stage = spirv_cross::CompilerMSL(words).get_execution_model();
        uint32_t group[3]{};
        const std::string source = translate(words, entry_for(stage), 0xffffffffu, group);
        // 32-byte AUREAMSL header, followed by UTF-8 MSL; padded to a word.
        std::ofstream output(argv[2], std::ios::binary);
        const char magic[] = "AUREAMSL";
        const uint32_t header[6] = {1, 0, static_cast<uint32_t>(source.size()), group[0], group[1], group[2]};
        output.write(magic, 8);
        output.write(reinterpret_cast<const char*>(header), sizeof(header));
        output.write(source.data(), source.size());
        const char pad[4]{};
        output.write(pad, (4 - source.size() % 4) % 4);
        if (!output) throw std::runtime_error("cannot write Metal blob");
        std::ofstream metal(std::string(argv[2]) + ".metal");
        metal << source;

        // Fragment shader that writes location 1 (MRT: 2D display + HDR scene)
        // ALSO gets `fs_main_c0`, the same shader with only location 0. Metal
        // refuses a pipeline whose fragment function writes a color attachment
        // the descriptor doesn't have (pixelFormat Invalid) - Vulkan and GLES
        // simply drop that write. The backend binds `fs_main_c0` in every
        // pipeline without a second color attachment (MetalResources.mm), so the
        // same GLSL serves the 3D MRT pass and a plain single-target pass.
        // The variant lives in a second .metal file linked into the same
        // .metallib (compile_metal.cmake); a stale file must never survive a
        // shader that no longer needs it, so it is removed first.
        const std::string c0Path = std::string(argv[2]) + ".c0.metal";
        std::remove(c0Path.c_str());
        if (writes_color1_or_more(words)) {
            const std::string c0 = translate(words, "fs_main_c0", 0x1u, nullptr);
            std::ofstream variant(c0Path);
            variant << c0;
            if (!variant) throw std::runtime_error("cannot write Metal color(0) variant");
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << argv[1] << ": " << e.what() << '\n';
        return 1;
    }
}
