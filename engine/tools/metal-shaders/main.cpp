// Host tool: preserve the GPUBackend binding ABI when translating to Metal.
// A tradução em si mora em shaderxlate/ShaderTranslate.cpp — a MESMA que o
// backend Metal usa em runtime para os shaders dos plugins.
#include "ShaderTranslate.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (size <= 0 || size % 4 != 0) {
        std::cerr << argv[1] << ": invalid SPIR-V file\n";
        return 1;
    }
    std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(words.data()), size);
    const aurea::xlate::Stage stage = aurea::xlate::spirv_stage(words.data(), words.size());
    if (stage == aurea::xlate::Stage::Unknown) {
        std::cerr << argv[1] << ": invalid SPIR-V module\n";
        return 1;
    }
    uint32_t group[3]{};
    std::string source, error;
    if (!aurea::xlate::spirv_to_msl(words.data(), words.size(), aurea::xlate::msl_entry_for(stage), 0xffffffffu,
                                    group, source, error)) {
        std::cerr << argv[1] << ": " << error << '\n';
        return 1;
    }
    // 32-byte AUREAMSL header, followed by UTF-8 MSL; padded to a word.
    std::ofstream output(argv[2], std::ios::binary);
    const char magic[] = "AUREAMSL";
    const uint32_t header[6] = {1, 0, static_cast<uint32_t>(source.size()), group[0], group[1], group[2]};
    output.write(magic, 8);
    output.write(reinterpret_cast<const char*>(header), sizeof(header));
    output.write(source.data(), static_cast<std::streamsize>(source.size()));
    const char pad[4]{};
    output.write(pad, static_cast<std::streamsize>((4 - source.size() % 4) % 4));
    if (!output) {
        std::cerr << argv[1] << ": cannot write Metal blob\n";
        return 1;
    }
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
    if (aurea::xlate::spirv_writes_color1_or_more(words.data(), words.size())) {
        std::string c0;
        if (!aurea::xlate::spirv_to_msl(words.data(), words.size(), "fs_main_c0", 0x1u, nullptr, c0, error)) {
            std::cerr << argv[1] << ": " << error << '\n';
            return 1;
        }
        std::ofstream variant(c0Path);
        variant << c0;
        if (!variant) {
            std::cerr << argv[1] << ": cannot write Metal color(0) variant\n";
            return 1;
        }
    }
    return 0;
}
