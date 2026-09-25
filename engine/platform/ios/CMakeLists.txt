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
option(AUREA_METAL_SHADERS "Traduz os shaders para Metal no build" ON)
set(AUREA_METAL_COMPILER "$ENV{AUREA_METAL_COMPILER}" CACHE FILEPATH "Compilador Metal do host")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# -----------------------------------------------------------------------------
# Compilador de Metal do HOST (SPIR-V → MSL). Ele traduz os shaders no build,
# e é a ÚNICA peça do iOS que compila para a máquina, não para o aparelho — por
# isso é construído à parte, com o compilador do host, num passo de configuração.
#
# Sem isto o configure para com "set AUREA_METAL_COMPILER": um pré-requisito
# manual que quebra o build na primeira tentativa num Mac limpo. Com isto, o
# único requisito externo continua sendo o glslc, que o motor já usa.
# -----------------------------------------------------------------------------
if(AUREA_METAL_SHADERS AND NOT AUREA_METAL_COMPILER)
    set(_mc_dir "${CMAKE_BINARY_DIR}/metal-compiler")
    set(_mc_bin "${_mc_dir}/aurea-metal-compiler")
    if(NOT EXISTS "${_mc_bin}")
        message(STATUS "Aurea iOS: construindo o compilador de Metal do host (SPIRV-Cross)")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -S "${AUREA_ROOT}/tools/metal-shaders" -B "${_mc_dir}"
                    -DCMAKE_BUILD_TYPE=Release
            RESULT_VARIABLE _mc_cfg
            OUTPUT_VARIABLE _mc_out ERROR_VARIABLE _mc_out)
        if(NOT _mc_cfg EQUAL 0)
            message(FATAL_ERROR
                "Nao foi possivel configurar o compilador de Metal do host.\n"
                "Ele busca o SPIRV-Cross (KhronosGroup/SPIRV-Cross) na primeira vez, "
                "entao precisa de rede e de um compilador C++ do host.\n${_mc_out}")
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${_mc_dir}" --config Release
            RESULT_VARIABLE _mc_build
            OUTPUT_VARIABLE _mc_out2 ERROR_VARIABLE _mc_out2)
        if(NOT _mc_build EQUAL 0)
            message(FATAL_ERROR "Falha ao compilar o compilador de Metal do host.\n${_mc_out2}")
        endif()
    endif()
    # Makefiles/Ninja escrevem o executável na raiz; o Visual Studio, em Release/.
    foreach(_cand "${_mc_bin}" "${_mc_bin}.exe" "${_mc_dir}/Release/aurea-metal-compiler.exe")
        if(EXISTS "${_cand}")
            set(AUREA_METAL_COMPILER "${_cand}" CACHE FILEPATH "Compilador Metal do host" FORCE)
            break()
        endif()
    endforeach()
    if(NOT AUREA_METAL_COMPILER)
        message(FATAL_ERROR
            "O compilador de Metal do host foi construido mas nao foi encontrado em ${_mc_dir}.\n"
            "Defina -DAUREA_METAL_COMPILER=<caminho> a mao.")
    endif()
    message(STATUS "Aurea iOS: compilador de Metal do host  ${AUREA_METAL_COMPILER}")
endif()

# -----------------------------------------------------------------------------
# O motor, como subdiretório. Ele detecta AUREA_PLATFORM_IOS pelo
# CMAKE_SYSTEM_NAME e já desliga o backend Vulkan e os testes do host.
# -----------------------------------------------------------------------------
set(AUREA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${AUREA_ROOT}" "${CMAKE_BINARY_DIR}/aurea-engine")

# Xcode links -lncnn in both configurations. Upstream adds a Debug-only "d"
# postfix, producing libncnnd.a even though all archives share build/core.
# Keep the iOS archive name stable without changing its Debug compiler flags.
if(TARGET ncnn)
    set_target_properties(ncnn PROPERTIES DEBUG_POSTFIX "")
endif()

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

endif()

# -----------------------------------------------------------------------------
# aurea_ios — a ponte. ARC nos .mm: a classe ObjC guarda o `Engine` num
# `std::unique_ptr`, e uma referência ObjC a mais (a view, o display link) sem
# ARC viraria vazamento ou uso depois do free.
# -----------------------------------------------------------------------------
add_library(aurea_ios STATIC
    bridge/AureaBridge.mm
    bridge/AureaEngine.mm
    bridge/AureaJSON.mm
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
target_compile_definitions(aurea_ios PRIVATE "$<$<CONFIG:Debug>:DEBUG=1>")
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
