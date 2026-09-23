# =============================================================================
#  AUREA — target iOS (engine/platform/ios)
#
#  A casca nativa do iOS: bridge ObjC++ ↔ C++, VideoToolbox, AVAudioEngine e o
#  host SwiftUI. O NÚCLEO é o mesmo do Android — este arquivo não compila uma
#  segunda versão do motor, ele compila o motor e a ponte.
#
#  Como este CMakeLists convive com o do motor (que NÃO é editado): ele é uma
#  árvore própria, e o diretório do motor entra como SUBDIRETÓRIO. Assim
#  `aurea_core`, os shaders e os terceiros são exatamente os mesmos alvos que o
#  Android usa, com as mesmas flags.
#
#     cmake -S engine/platform/ios -B <build> \
#           -DCMAKE_TOOLCHAIN_FILE=engine/platform/ios/ios_toolchain.cmake \
#           -DAUREA_GLSLC=/caminho/do/glslc
#     cmake --build <build>
#
#  Alvos:
#    aurea_thirdparty  meshoptimizer, ufbx, basisu, harfbuzz (do motor)
#    aurea_core        o núcleo: timeline, playback, FrameGraph, efeitos, media
#    aurea_metal       o backend Metal (gpu/metal — escrito pela sessão paralela)
#    aurea_ios         a ponte: AureaBridge, AureaEngine, IOSSurfaceView,
#                      IOSVideoDecoder (VideoToolbox), IOSAudio (AVAudioEngine)
#
#  O que o Xcode consome é `libaurea_ios.a` (mais os .a das dependências), pelo
#  script de build do target do app — ver Aurea.xcodeproj/project.pbxproj.
# =============================================================================
cmake_minimum_required(VERSION 3.22)

# OBJCXX: a ponte é ObjC++ de verdade (ARC nos .mm), não C++ com extensão.
project(AureaIOS
        VERSION 2.0.0
        DESCRIPTION "Aurea Editor — casca nativa do iOS e ponte para o nucleo"
        LANGUAGES C CXX OBJCXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR
        "Este diretorio so compila para iOS. Use:\n"
        "  cmake -S engine/platform/ios -B <build> "
        "-DCMAKE_TOOLCHAIN_FILE=engine/platform/ios/ios_toolchain.cmake")
endif()

set(AUREA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "Raiz do motor (engine/)")

# -----------------------------------------------------------------------------
# Opções
# -----------------------------------------------------------------------------
option(AUREA_GPU_METAL "Compila e liga o backend Metal (gpu/metal)" ON)
# Ponto de extensão dos shaders MSL: o SPIR-V embutido pelo motor é traduzido
# para MSL no build (SPIRV-Cross), como o MetalBackend espera. O script e a
# ferramenta NÃO entram nesta entrega — ligar a opção sem eles falha no
# configure, com a mensagem dizendo exatamente o que falta. Ver README.
option(AUREA_METAL_SHADERS "Gera MSL do SPIR-V no build (exige SPIRV-Cross)" OFF)
set(AUREA_SPIRV_CROSS "" CACHE FILEPATH "Binario spirv-cross (para AUREA_METAL_SHADERS)")
set(AUREA_METAL_SHADER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/cmake/AureaMetalShaders.cmake"
    CACHE FILEPATH "Script que gera os .metal do SPIR-V")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# -----------------------------------------------------------------------------
# O motor, como subdiretório. Ele detecta AUREA_PLATFORM_IOS pelo
# CMAKE_SYSTEM_NAME e já desliga o backend Vulkan e os testes do host.
# -----------------------------------------------------------------------------
set(AUREA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${AUREA_ROOT}" "${CMAKE_BINARY_DIR}/aurea-engine")

# -----------------------------------------------------------------------------
# aurea_metal — backend Metal (gpu/metal). O núcleo não depende dele: quem
# depende é a ponte, e só ela conhece `mtl::create_backend`.
# -----------------------------------------------------------------------------
set(AUREA_METAL_DIR "${AUREA_ROOT}/gpu/metal")

if(AUREA_GPU_METAL)
    if(NOT EXISTS "${AUREA_METAL_DIR}/MetalBackend.hpp")
        message(FATAL_ERROR
            "AUREA_GPU_METAL=ON mas ${AUREA_METAL_DIR}/MetalBackend.hpp nao existe.\n"
            "O backend Metal e a sessao paralela que escreve gpu/metal/. Enquanto ele "
            "nao estiver la, configure com -DAUREA_GPU_METAL=OFF:\n"
            "  o nucleo compila, e a ponte (AureaBridge.mm) precisa do backend para "
            "criar o dispositivo — compilar sem ele serve para conferir o resto.")
    endif()

    # O alvo `aurea_metal` e definido pelo DONO do backend (gpu/metal/CMakeLists.txt,
    # com os fontes, o ARC e os frameworks dele). Aqui so se garante que ele exista:
    # se o integrador ja tiver posto `add_subdirectory(gpu/metal)` no CMakeLists do
    # motor (como o contrato do backend pede), o alvo ja esta de pe e nada e feito —
    # criar de novo daria "target already exists".
    if(NOT TARGET aurea_metal)
        add_subdirectory("${AUREA_METAL_DIR}" "${CMAKE_BINARY_DIR}/aurea-metal")
    endif()
    if(AUREA_METAL_SHADERS)
        if(NOT AUREA_SPIRV_CROSS OR NOT EXISTS "${AUREA_SPIRV_CROSS}")
            message(FATAL_ERROR
                "AUREA_METAL_SHADERS=ON exige -DAUREA_SPIRV_CROSS=<binario>. "
                "O spirv-cross nao vem no SDK da Apple; baixe em "
                "https://github.com/KhronosGroup/SPIRV-Cross e aponte aqui.")
        endif()
        if(NOT EXISTS "${AUREA_METAL_SHADER_SCRIPT}")
            message(FATAL_ERROR
                "AUREA_METAL_SHADERS=ON exige ${AUREA_METAL_SHADER_SCRIPT}.\n"
                "O ponto de extensao esta NOMEADO: o script recebe AUREA_METAL_SPV_DIR "
                "(os .spv que o motor ja gera em generated/shaders) e AUREA_SPIRV_CROSS, "
                "e emite os .metal em AUREA_METAL_OUT_DIR. Ele ainda nao foi escrito.")
        endif()
        # O ponto de extensão: quando o script existir, é aqui que a tradução
        # SPIR-V → MSL entra no build. Os .spv saem de `AUREA_SHADER_SPV_DIR`,
        # que cmake/AureaShaders.cmake publica no escopo do subdiretório.
        add_custom_command(
            OUTPUT "${CMAKE_BINARY_DIR}/metal-shaders/.stamp"
            COMMAND ${CMAKE_COMMAND}
                    "-DAUREA_METAL_SPV_DIR=${AUREA_SHADER_SPV_DIR}"
                    "-DAUREA_METAL_OUT_DIR=${CMAKE_BINARY_DIR}/metal-shaders"
                    "-DAUREA_SPIRV_CROSS=${AUREA_SPIRV_CROSS}"
                    -P "${AUREA_METAL_SHADER_SCRIPT}"
            COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/metal-shaders/.stamp"
            DEPENDS ${AUREA_SHADER_SPV_DIR}
            COMMENT "SPIR-V -> MSL (seu script: ${AUREA_METAL_SHADER_SCRIPT})"
            VERBATIM)
        add_custom_target(aurea_metal_shaders ALL
            DEPENDS "${CMAKE_BINARY_DIR}/metal-shaders/.stamp")
        add_dependencies(aurea_metal aurea_metal_shaders)
    endif()
endif()

# -----------------------------------------------------------------------------
# aurea_ios — a ponte. ARC nos .mm: a classe ObjC guarda o `Engine` num
# `std::unique_ptr`, e uma referência ObjC a mais (a view, o display link) sem
# ARC viraria vazamento ou uso depois do free.
# -----------------------------------------------------------------------------
add_library(aurea_ios STATIC
    bridge/AureaBridge.mm
    bridge/AureaEngine.mm
    bridge/IOSSurfaceView.mm
    bridge/IOSVideoDecoder.mm
    bridge/IOSAudio.mm
)

target_include_directories(aurea_ios
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/bridge"
        "${AUREA_ROOT}/include"
    PRIVATE
        "${AUREA_ROOT}/src"
)

target_compile_options(aurea_ios PRIVATE
    -Wall -Wextra -Wpedantic -fno-exceptions
    "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>"
    "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX,OBJCXX>>:-O3>"
    "$<$<AND:$<CONFIG:RelWithDebInfo>,$<COMPILE_LANGUAGE:CXX,OBJCXX>>:-O2 -g>"
)

target_link_libraries(aurea_ios PUBLIC aurea_core)
if(AUREA_GPU_METAL)
    target_link_libraries(aurea_ios PUBLIC aurea_metal)
    target_compile_definitions(aurea_ios PUBLIC AUREA_GPU_METAL=1)
endif()

# Frameworks da Apple. VideoToolbox e CoreMedia no decode/encode; AVFoundation
# no AVAudioEngine e no AVAssetWriter; Metal e QuartzCore no CAMetalLayer.
foreach(_fw Metal MetalKit QuartzCore CoreVideo CoreMedia VideoToolbox
            AVFoundation AudioToolbox CoreAudio Foundation UIKit)
    target_link_libraries(aurea_ios PUBLIC "-framework ${_fw}")
endforeach()

# -----------------------------------------------------------------------------
# Resumo do que foi configurado (o integrador lê isto no primeiro configure).
# -----------------------------------------------------------------------------
message(STATUS "Aurea iOS: ${CMAKE_SYSTEM_NAME} ${CMAKE_OSX_SYSROOT} ${CMAKE_OSX_ARCHITECTURES} "
               "piso ${CMAKE_OSX_DEPLOYMENT_TARGET}")
message(STATUS "Aurea iOS: backend Metal           ${AUREA_GPU_METAL}")
message(STATUS "Aurea iOS: shaders MSL no build    ${AUREA_METAL_SHADERS}")
