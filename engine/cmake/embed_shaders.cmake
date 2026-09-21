# =============================================================================
#  embed_shaders.cmake — roda em modo script (-P) no build.
#
#  Lê os .spv (na ordem do enum ShaderId) e escreve um .cpp com os bytes. Os
#  arrays são `alignas(4)` porque o Vulkan lê SPIR-V como palavras de 32 bits.
# =============================================================================
set(_out "// GERADO por cmake/embed_shaders.cmake — nao editar.\n")
string(APPEND _out "#include \"aurea/shaders/ShaderIds.hpp\"\n\nnamespace aurea {\nnamespace {\n\n")

set(_table "")
set(_i 0)
foreach(_f IN LISTS SPV_FILES)
    file(READ "${_f}" _hex HEX)
    string(LENGTH "${_hex}" _hexlen)
    math(EXPR _bytes "${_hexlen} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _arr "${_hex}")
    # Quebra de linha a cada 32 bytes, para o arquivo gerado continuar legível
    # num diff e não estourar o limite de linha de alguns compiladores.
    string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){32})" "\\1\n    " _arr "${_arr}")
    string(APPEND _out "alignas(4) const unsigned char kBlob${_i}[] = {\n    ${_arr}\n};\n\n")
    string(APPEND _table "    {reinterpret_cast<const u32*>(kBlob${_i}), ${_bytes}},\n")
    math(EXPR _i "${_i} + 1")
endforeach()

string(APPEND _out "const ShaderBlob kBlobs[] = {\n${_table}};\n\n")
string(APPEND _out "static_assert(sizeof(kBlobs) / sizeof(kBlobs[0]) == kShaderCount,\n")
string(APPEND _out "              \"lista de SPIR-V fora de sincronia com ShaderId\");\n\n")
string(APPEND _out "} // namespace\n\n")
string(APPEND _out "const ShaderBlob& shader_blob(ShaderId id) noexcept {\n")
string(APPEND _out "    return kBlobs[static_cast<u32>(id)];\n}\n\n} // namespace aurea\n")

file(WRITE "${OUT_CPP}" "${_out}")
