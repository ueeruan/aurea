# Modelo de profundidade para o MSVC (host de testes), que não tem `.incbin`.
# Os bytes vão em hexadecimal, em pedaços de 8000 bytes (16000 caracteres,
# abaixo do limite de 16380 de um literal do MSVC), sem regex por byte: o
# script só copia o hex que o próprio CMake lê. O .cpp decodifica uma vez, na
# primeira chamada. Android e iOS usam cmake/AiDepthModel.incbin.cpp.in.
set(chunk 8000)
file(WRITE "${OUTPUT}" "// Gerado por engine/cmake/EmbedDepthModel.cmake.\n#include <cstddef>\n#include <vector>\nnamespace {\n")
foreach(name IN ITEMS bin param)
    if(name STREQUAL "bin")
        set(path "${BIN}")
    else()
        set(path "${PARAM}")
    endif()
    file(SIZE "${path}" size)
    file(APPEND "${OUTPUT}" "constexpr std::size_t k_${name}_size = ${size};\nconst char* const k_${name}_hex[] = {\n")
    set(offset 0)
    set(buffer "")
    set(pending 0)
    while(offset LESS size)
        file(READ "${path}" data OFFSET ${offset} LIMIT ${chunk} HEX)
        string(APPEND buffer "\"${data}\",\n")
        math(EXPR offset "${offset} + ${chunk}")
        math(EXPR pending "${pending} + 1")
        if(pending EQUAL 64)
            file(APPEND "${OUTPUT}" "${buffer}")
            set(buffer "")
            set(pending 0)
        endif()
    endwhile()
    file(APPEND "${OUTPUT}" "${buffer}};\n")
endforeach()
file(APPEND "${OUTPUT}" [=[
int nibble(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }
std::vector<unsigned char> decode(const char* const* parts, std::size_t size, bool terminate) {
    std::vector<unsigned char> out;
    out.reserve(size + 1);
    for (std::size_t i = 0; out.size() < size; ++i)
        for (const char* p = parts[i]; p[0] && p[1] && out.size() < size; p += 2)
            out.push_back(static_cast<unsigned char>(nibble(p[0]) << 4 | nibble(p[1])));
    if (terminate) out.push_back(0);
    return out;
}
}
namespace aurea::ai::embedded {
const unsigned char* depth_model_weights() {
    static const std::vector<unsigned char> bytes = decode(k_bin_hex, k_bin_size, false);
    return bytes.data();
}
std::size_t depth_model_weights_size() { return k_bin_size; }
const char* depth_model_param() {
    static const std::vector<unsigned char> text = decode(k_param_hex, k_param_size, true);
    return reinterpret_cast<const char*>(text.data());
}
}
]=])
