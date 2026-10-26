# Modelos do AureaHyper para o MSVC (host de testes), que não tem `.incbin`.
# Mesmo esquema de cmake/EmbedDepthModel.cmake: hexadecimal em pedaços de
# 8000 bytes, decodificado uma vez na primeira chamada. Android e iOS usam o
# `.incbin` gerado em cmake/AureaHyperNeural.cmake.
#   -DROOT=<engine/assets/hyper> -DFILES=<a|b|c> -DOUTPUT=<.cpp>
string(REPLACE "|" ";" files "${FILES}")
set(chunk 8000)
file(WRITE "${OUTPUT}" "// Gerado por engine/cmake/EmbedHyperAssets.cmake.\n#include <cstddef>\n#include <cstring>\n#include <mutex>\n#include <vector>\nnamespace {\n")
set(index 0)
foreach(rel IN LISTS files)
    set(path "${ROOT}/${rel}")
    file(SIZE "${path}" size)
    file(APPEND "${OUTPUT}" "constexpr std::size_t k_size_${index} = ${size};\nconst char* const k_hex_${index}[] = {\n")
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
    file(APPEND "${OUTPUT}" "${buffer}nullptr};\n")
    math(EXPR index "${index} + 1")
endforeach()
file(APPEND "${OUTPUT}" [=[
int nibble(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }
struct Entry { const char* id; const char* const* hex; std::size_t size; };
const Entry kEntries[] = {
]=])
set(index 0)
foreach(rel IN LISTS files)
    file(APPEND "${OUTPUT}" "    {\"${rel}\", k_hex_${index}, k_size_${index}},\n")
    math(EXPR index "${index} + 1")
endforeach()
file(APPEND "${OUTPUT}" [=[
};
constexpr std::size_t kCount = sizeof(kEntries) / sizeof(kEntries[0]);
std::vector<unsigned char> g_bytes[kCount];
std::once_flag g_once[kCount];
}
bool aurea_hyper_embedded_lookup(const char* id, const unsigned char** data, std::size_t* size) {
    for (std::size_t i = 0; i < kCount; ++i) {
        if (std::strcmp(kEntries[i].id, id) != 0) continue;
        std::call_once(g_once[i], [i] {
            std::vector<unsigned char>& out = g_bytes[i];
            const std::size_t n = kEntries[i].size;
            out.reserve(n + 1);
            for (std::size_t k = 0; kEntries[i].hex[k] && out.size() < n; ++k)
                for (const char* p = kEntries[i].hex[k]; p[0] && p[1] && out.size() < n; p += 2)
                    out.push_back(static_cast<unsigned char>(nibble(p[0]) << 4 | nibble(p[1])));
            out.push_back(0);   // texto (.param) termina em NUL
        });
        *data = g_bytes[i].data();
        *size = kEntries[i].size;
        return true;
    }
    return false;
}
]=])
