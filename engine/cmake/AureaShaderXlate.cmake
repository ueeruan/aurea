# =============================================================================
#  AureaShaderXlate.cmake — SPIRV-Cross + shaderxlate/ShaderTranslate.cpp.
#
#  O MESMO alvo serve às ferramentas do host (tools/metal-shaders, que compila
#  aurea-gles-compiler e aurea-metal-compiler) e aos backends que traduzem em
#  runtime os shaders dos plugins (GLES no Android, Metal no iOS). Uma versão
#  do SPIRV-Cross, uma lista de arquivos, as mesmas opções: o shader que sai
#  em runtime é o que sairia no build.
#
#  SPIRV-Cross: Apache-2.0 (docs/plugins/README.md, "Licenças"). Fixado num
#  commit e no SHA-256 do arquivo — nada de branch que anda.
#
#  Resultado: `aurea_shader_xlate`, biblioteca OBJECT. Quem a liga recebe os
#  objetos dentro do próprio alvo (um .a a menos para o Xcode listar) e o
#  diretório de ShaderTranslate.hpp. AUREA_XLATE_MSL liga a tradução para MSL
#  (Apple e host de testes); o Android não a leva.
# =============================================================================
include_guard(GLOBAL)
include(FetchContent)

set(AUREA_SPIRV_CROSS_COMMIT d8e3e2b141b8c8a167b2e3984736a6baacff316c)
# SOURCE_SUBDIR inexistente: o FetchContent só baixa; os arquivos que o motor
# usa são compilados abaixo (sem CLI, testes nem os backends HLSL/C++/C).
FetchContent_Declare(aurea_spvc
    URL https://github.com/KhronosGroup/SPIRV-Cross/archive/${AUREA_SPIRV_CROSS_COMMIT}.tar.gz
    URL_HASH SHA256=18e4abb4ae97451c3d90bb23eb1c22859c8ef08bd5d8d3ec09e035846bbf6556
    SOURCE_SUBDIR aurea-sem-cmake)
FetchContent_MakeAvailable(aurea_spvc)
FetchContent_GetProperties(aurea_spvc)

if(NOT DEFINED AUREA_XLATE_MSL)
    if(ANDROID)
        set(AUREA_XLATE_MSL OFF)
    else()
        set(AUREA_XLATE_MSL ON)
    endif()
endif()

set(_spvc "${aurea_spvc_SOURCE_DIR}")
set(_xlate_sources
    "${_spvc}/spirv_cross.cpp"
    "${_spvc}/spirv_parser.cpp"
    "${_spvc}/spirv_cross_parsed_ir.cpp"
    "${_spvc}/spirv_cfg.cpp"
    "${_spvc}/spirv_glsl.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../shaderxlate/ShaderTranslate.cpp")
if(AUREA_XLATE_MSL)
    list(APPEND _xlate_sources "${_spvc}/spirv_msl.cpp")
endif()

add_library(aurea_shader_xlate OBJECT ${_xlate_sources})
target_compile_features(aurea_shader_xlate PRIVATE cxx_std_17)
target_include_directories(aurea_shader_xlate
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../shaderxlate"
    PRIVATE "${_spvc}")
if(AUREA_XLATE_MSL)
    target_compile_definitions(aurea_shader_xlate PRIVATE AUREA_XLATE_MSL=1)
endif()
set_target_properties(aurea_shader_xlate PROPERTIES POSITION_INDEPENDENT_CODE ON)
# O SPIRV-Cross reporta erro por exceção: aqui elas ficam LIGADAS (e só aqui;
# ShaderTranslate.cpp as prende antes de qualquer fronteira do motor). Código
# de terceiro: sem os avisos do motor; -Os no aparelho (tamanho do binário).
target_compile_options(aurea_shader_xlate PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc /W0 /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fexceptions -w>
    $<$<AND:$<BOOL:${ANDROID}>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-Os -ffunction-sections -fdata-sections>
    $<$<AND:$<BOOL:${AUREA_PLATFORM_IOS}>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-Os>)
