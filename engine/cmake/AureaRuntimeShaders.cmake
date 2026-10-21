# =============================================================================
#  AureaRuntimeShaders.cmake — o glslang que compila no APARELHO os shaders
#  dos plugins (GLSL 450 → SPIR-V). Incluído depois do AureaNcnn.cmake.
#
#  UM glslang só no binário. O ncnn (IA de profundidade) já traz o glslang
#  fixado em AureaNcnn.cmake quando a inferência Vulkan está ligada (Android e
#  host): o motor usa ESSE alvo — o mesmo código, sem um segundo glslang (que
#  ainda daria símbolos duplicados no link estático). Onde o ncnn não o
#  compila (iOS: Metal, sem Vulkan), o MESMO arquivo fixado (mesma URL e
#  SHA-256) é compilado aqui, só com o necessário: front-end GLSL + gerador de
#  SPIR-V. Sem HLSL, sem spirv-opt, sem executáveis.
#
#  glslang: BSD-3-Clause e outras permissivas (docs/plugins/README.md,
#  "Licenças").
#
#  Resultado: AUREA_RUNTIME_SHADERS_READY e AUREA_RUNTIME_GLSLANG_LINK (os
#  alvos que aurea_core liga DIRETAMENTE em PRIVATE — uma biblioteca OBJECT só
#  entrega os objetos a quem a liga direto).
# =============================================================================
include_guard(GLOBAL)

option(AUREA_RUNTIME_SHADERS "Compila no aparelho os shaders dos plugins (glslang)" ON)
set(AUREA_RUNTIME_SHADERS_READY OFF)
set(AUREA_RUNTIME_GLSLANG_LINK "")
if(NOT AUREA_RUNTIME_SHADERS)
    return()
endif()

if(TARGET glslang AND TARGET glslang-default-resource-limits)
    # O glslang do ncnn: já está no link do aplicativo.
    set(AUREA_RUNTIME_GLSLANG_LINK glslang glslang-default-resource-limits)
    set(AUREA_RUNTIME_SHADERS_READY ON)
    message(STATUS "Aurea: shaders de plugin com o glslang do ncnn")
    return()
endif()

include(FetchContent)
# A MESMA revisão que AureaNcnn.cmake fixa para o ncnn (URL e SHA-256).
FetchContent_Declare(aurea_rtgl
    URL https://codeload.github.com/nihui/glslang/zip/a9ac7d5f307e5db5b8c4fbf904bdba8fca6283bc
    URL_HASH SHA256=7799500ebd4ba8b7b0e32503f33724d2308dd1da30cd5f22215014d5d2c40670
    SOURCE_SUBDIR aurea-sem-cmake)
FetchContent_MakeAvailable(aurea_rtgl)
FetchContent_GetProperties(aurea_rtgl)
set(_g "${aurea_rtgl_SOURCE_DIR}")

# glslang/build_info.h: o que o build_info.py do glslang gera, sem Python — a
# versão sai da primeira entrada do CHANGES.md da revisão fixada.
file(STRINGS "${_g}/CHANGES.md" _g_versions REGEX "^## [0-9]+\\.[0-9]+\\.[0-9]+")
list(GET _g_versions 0 _g_version)
string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" _g_match "${_g_version}")
set(major ${CMAKE_MATCH_1})
set(minor ${CMAKE_MATCH_2})
set(patch ${CMAKE_MATCH_3})
set(flavor "")
set(_g_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/glslang-include")
configure_file("${_g}/build_info.h.tmpl" "${_g_generated}/glslang/build_info.h" @ONLY)

file(GLOB _g_sources CONFIGURE_DEPENDS
    "${_g}/glslang/GenericCodeGen/*.cpp"
    "${_g}/glslang/MachineIndependent/*.cpp"
    "${_g}/glslang/MachineIndependent/preprocessor/*.cpp"
    "${_g}/glslang/ResourceLimits/ResourceLimits.cpp")
list(APPEND _g_sources
    "${_g}/SPIRV/GlslangToSpv.cpp"
    "${_g}/SPIRV/InReadableOrder.cpp"
    "${_g}/SPIRV/Logger.cpp"
    "${_g}/SPIRV/SpvBuilder.cpp"
    "${_g}/SPIRV/SpvPostProcess.cpp"
    "${_g}/SPIRV/SpvTools.cpp"
    "${_g}/SPIRV/disassemble.cpp"
    "${_g}/SPIRV/doc.cpp")
if(WIN32)
    list(APPEND _g_sources "${_g}/glslang/OSDependent/Windows/ossource.cpp")
    set(_g_os GLSLANG_OSINCLUDE_WIN32)
else()
    list(APPEND _g_sources "${_g}/glslang/OSDependent/Unix/ossource.cpp")
    set(_g_os GLSLANG_OSINCLUDE_UNIX)
endif()

# OBJECT: os objetos entram no próprio aurea_core (libaurea_core.a), que o
# projeto do Xcode já lista — nenhum arquivo novo para ligar no app.
add_library(aurea_glslang_objects OBJECT ${_g_sources})
target_compile_features(aurea_glslang_objects PRIVATE cxx_std_17)
target_include_directories(aurea_glslang_objects PUBLIC "${_g}" "${_g_generated}")
target_compile_definitions(aurea_glslang_objects PUBLIC ENABLE_SPIRV=1 ENABLE_OPT=0 ${_g_os})
set_target_properties(aurea_glslang_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(aurea_glslang_objects PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W0 /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w -fno-exceptions -Os>)
set(AUREA_RUNTIME_GLSLANG_LINK aurea_glslang_objects)
set(AUREA_RUNTIME_SHADERS_READY ON)
message(STATUS "Aurea: shaders de plugin com o glslang compilado pelo motor (${major}.${minor}.${patch})")
