// Host tool: the same optimized SPIR-V used by Vulkan/Metal becomes ESSL 3.10.
// Binding remapping is confined to this backend's ABI, never effect sources.
// A tradução em si mora em shaderxlate/ShaderTranslate.cpp — a MESMA que o
// backend GLES usa em runtime para os shaders dos plugins.
#include "ShaderTranslate.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool load(const char* path, std::vector<uint32_t>& words, std::string& error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (size < 20 || size % 4) { error = "invalid SPIR-V file"; return false; }
    words.resize(static_cast<size_t>(size) / 4);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(words.data()), size)) { error = "SPIR-V read failed"; return false; }
    if (words[0] != 0x07230203u) { error = "invalid SPIR-V magic"; return false; }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--supports-shared-spv-key") return 0;
    // Optional argument: the shared optimized SPIR-V lookup key when ESSL is
    // translated from a semantically identical, driver-compatible module.
    if (argc != 3 && argc != 4) return 2;
    std::string error;
    std::vector<uint32_t> words, keyWords;
    if (!load(argv[1], words, error) || (argc == 4 && !load(argv[3], keyWords, error))) {
        std::cerr << argv[1] << ": " << error << '\n';
        return 1;
    }
    std::string source;
    const bool ok = argc == 4
        ? aurea::xlate::spirv_to_essl(words.data(), words.size(), source, error, keyWords.data(), keyWords.size())
        : aurea::xlate::spirv_to_essl(words.data(), words.size(), source, error);
    if (!ok) {
        std::cerr << argv[1] << ": " << error << '\n';
        return 1;
    }
    std::ofstream output(argv[2], std::ios::binary);
    output.write(source.data(), static_cast<std::streamsize>(source.size()));
    if (!output) {
        std::cerr << argv[1] << ": ESSL write failed\n";
        return 1;
    }
    return 0;
}
