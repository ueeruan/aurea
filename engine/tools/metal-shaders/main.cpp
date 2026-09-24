// Host tool: preserve the GPUBackend binding ABI when translating to Metal.
#include "spirv_msl.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try {
        std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
        const auto size = input.tellg();
        if (size <= 0 || size % 4 != 0) throw std::runtime_error("invalid SPIR-V file");
        std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
        input.seekg(0);
        input.read(reinterpret_cast<char*>(words.data()), size);
        spirv_cross::CompilerMSL compiler(std::move(words));
        const auto stage = compiler.get_execution_model();
        const char* entry = stage == spv::ExecutionModelVertex ? "vs_main"
                          : stage == spv::ExecutionModelFragment ? "fs_main" : "cs_main";
        compiler.rename_entry_point("main", entry, stage);
        auto common = compiler.get_common_options();
        // The engine's projection and full-screen triangle use Vulkan NDC:
        // y=-1 is the top edge. Metal's viewport places y=+1 at the top.
        common.vertex.flip_vert_y = true;
        compiler.set_common_options(common);
        spirv_cross::CompilerMSL::Options options;
        options.platform = spirv_cross::CompilerMSL::Options::iOS;
        options.set_msl_version(2, 1);
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
        uint32_t group[3]{};
        if (stage == spv::ExecutionModelGLCompute) {
            for (unsigned i = 0; i < 3; ++i)
                group[i] = compiler.get_execution_mode_argument(spv::ExecutionModeLocalSize, i);
            if (!group[0] || !group[1] || !group[2]) throw std::runtime_error("missing compute group size");
        }
        const auto source = compiler.compile();
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
        return 0;
    } catch (const std::exception& e) {
        std::cerr << argv[1] << ": " << e.what() << '\n';
        return 1;
    }
}
