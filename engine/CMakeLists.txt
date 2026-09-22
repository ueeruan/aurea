# =============================================================================
#  AUREA ENGINE — núcleo C++23 compartilhado entre Android e iOS.
#
#     CPU organiza. GPU processa. Hardware decodifica. Hardware codifica.
#     UI apenas controla.
#
#  Alvos:
#    aurea_core    tudo que independe de API gráfica e de SO: timeline,
#                  playback, FrameGraph, EffectGraph, efeitos, renderer (sobre
#                  GPUBackend), SPIR-V embutido. Roda nos testes do host.
#    aurea_vulkan  o backend Vulkan. O MESMO código no Android e no host de
#                  testes (sem superfície), para os testes visuais usarem os
#                  shaders reais.
#    aurea         a .so do Android: JNI + MediaCodec + render thread.
# =============================================================================
cmake_minimum_required(VERSION 3.22)

project(AureaEngine
        VERSION 2.0.0
        DESCRIPTION "Aurea Editor — motor de composicao e render"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(ANDROID)
    set(AUREA_PLATFORM_ANDROID ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(AUREA_PLATFORM_IOS ON)
else()
    set(AUREA_PLATFORM_HOST ON)
endif()

option(AUREA_BUILD_TESTS      "Compila os testes do motor"      ON)
option(AUREA_ENABLE_SANITIZE  "ASan/UBSan no host"              OFF)

include(cmake/AureaShaders.cmake)

# -----------------------------------------------------------------------------
# Shaders: GLSL → SPIR-V → .cpp embutido (ver cmake/AureaShaders.cmake).
# -----------------------------------------------------------------------------
aurea_compile_shaders(AUREA_SHADER_SOURCES AUREA_GENERATED_INCLUDE)

# -----------------------------------------------------------------------------
# aurea_core
# -----------------------------------------------------------------------------
set(AUREA_CORE_SOURCES
    src/core/Log.cpp
    src/core/Time.cpp
    src/core/Thread.cpp
    src/memory/MemoryManager.cpp
    src/jobs/JobSystem.cpp
    src/command/History.cpp
    src/platform/DeviceCapabilities.cpp
    src/animation/Curve.cpp
    src/animation/Track.cpp
    src/timeline/Composition.cpp
    src/timeline/Timeline.cpp
    src/project/Project.cpp
    src/project/Serialization.cpp
    src/render/GPUCapabilities.cpp
    src/render/FrameGraph.cpp
    src/render/ShaderLibrary.cpp
    src/render/RenderScheduler.cpp
    src/render/Renderer.cpp
    src/effects/Parameter.cpp
    src/effects/EffectGraph.cpp
    src/effects/builtin/ColorEffects.cpp
    src/effects/builtin/BlurEffects.cpp
    src/effects/builtin/GlowEffect.cpp
    src/effects/builtin/TransformEffect.cpp
    src/effects/builtin/MotionTileEffect.cpp
    src/media/DecodedFrameCache.cpp
    src/media/VideoSource.cpp
    src/media/MediaManager.cpp
    src/media/ThumbnailService.cpp
    src/audio/AudioCore.cpp
    src/audio/AudioBlockCache.cpp
    src/audio/AudioMixer.cpp
    src/audio/AudioEngine.cpp
    src/audio/Waveform.cpp
    src/playback/Playback.cpp
    src/scene3d/GltfImporter.cpp
    src/scene3d/SceneRenderer.cpp
    src/scene3d/Environment.cpp
    src/scene3d/Animation.cpp
    src/engine/Engine.cpp
    ${AUREA_SHADER_SOURCES}
)

# -----------------------------------------------------------------------------
# Terceiros (third_party/README.md). Compilados à parte, com os avisos deles:
# o /W4 do motor não vale para código que não é nosso.
# -----------------------------------------------------------------------------
file(GLOB AUREA_MESHOPT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/meshoptimizer/src/*.cpp)
add_library(aurea_thirdparty STATIC ${AUREA_MESHOPT_SOURCES})
target_include_directories(aurea_thirdparty PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/meshoptimizer/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_compile_options(aurea_thirdparty PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W0 /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w -fno-exceptions>
    $<$<AND:$<CONFIG:Release,RelWithDebInfo>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-O2>)
set_target_properties(aurea_thirdparty PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(aurea_core STATIC ${AUREA_CORE_SOURCES})
target_link_libraries(aurea_core PUBLIC aurea_thirdparty)

target_include_directories(aurea_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${AUREA_GENERATED_INCLUDE}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(aurea_core
    PUBLIC
        AUREA_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        AUREA_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        AUREA_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)

if(AUREA_PLATFORM_ANDROID)
    target_compile_definitions(aurea_core PUBLIC AUREA_PLATFORM_ANDROID=1)
elseif(AUREA_PLATFORM_IOS)
    target_compile_definitions(aurea_core PUBLIC AUREA_PLATFORM_IOS=1)
else()
    target_compile_definitions(aurea_core PUBLIC AUREA_PLATFORM_HOST=1)
endif()

# Exceções DESLIGADAS: nenhuma fronteira do motor pode lançar. /utf-8: o fonte
# tem acentos nos comentários, e sem a flag o MSVC lê com a página de código do
# sistema e o erro aparece longe da causa.
set(AUREA_WARNINGS
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc- /Zc:__cplusplus /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -fno-exceptions>
)
target_compile_options(aurea_core PRIVATE ${AUREA_WARNINGS})
target_compile_options(aurea_core PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-O3>
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-O2 -g>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2>
)

if(AUREA_ENABLE_SANITIZE AND NOT ANDROID AND NOT AUREA_PLATFORM_IOS)
    target_compile_options(aurea_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(aurea_core PRIVATE -fsanitize=address,undefined)
endif()

# `pthread` separado só no Linux/macOS de host. No Android e no iOS as threads
# vivem na libc, e pedir -lpthread quebra o link.
if(NOT MSVC AND NOT ANDROID AND NOT AUREA_PLATFORM_IOS)
    find_package(Threads REQUIRED)
    target_link_libraries(aurea_core PUBLIC Threads::Threads)
endif()

# -----------------------------------------------------------------------------
# aurea_vulkan — backend Vulkan (Android e host de testes).
#
# No Android os cabeçalhos vêm do sysroot do NDK. No host: VULKAN_SDK, ou os
# cabeçalhos do NDK declarado em android/local.properties (os mesmos que o
# aparelho usa). A biblioteca é carregada em runtime (VulkanLoader), então o
# host não precisa de SDK instalado — só do driver da GPU.
# -----------------------------------------------------------------------------
set(AUREA_HAS_VULKAN OFF)
set(AUREA_VULKAN_INCLUDE "")
if(AUREA_PLATFORM_ANDROID)
    set(AUREA_HAS_VULKAN ON)
elseif(AUREA_PLATFORM_HOST)
    if(AUREA_VULKAN_HEADERS AND EXISTS "${AUREA_VULKAN_HEADERS}/vulkan/vulkan.h")
        set(AUREA_VULKAN_INCLUDE "${AUREA_VULKAN_HEADERS}")
    elseif(DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}/Include/vulkan/vulkan.h")
        set(AUREA_VULKAN_INCLUDE "$ENV{VULKAN_SDK}/Include")
    elseif(AUREA_GLSLC)
        # glslc do NDK: <ndk>/shader-tools/<host>/glslc → sysroot ao lado.
        get_filename_component(_ndk_root "${AUREA_GLSLC}" DIRECTORY)
        get_filename_component(_ndk_root "${_ndk_root}" DIRECTORY)
        get_filename_component(_ndk_root "${_ndk_root}" DIRECTORY)
        file(GLOB _sysroots "${_ndk_root}/toolchains/llvm/prebuilt/*/sysroot/usr/include")
        foreach(_s IN LISTS _sysroots)
            if(EXISTS "${_s}/vulkan/vulkan.h")
                # Só as pastas do Vulkan, copiadas para o build: incluir o
                # sysroot inteiro traria a libc do Android (bionic) por cima da
                # libc do host, e nada mais compilaria.
                set(_iso "${CMAKE_CURRENT_BINARY_DIR}/generated/vulkan-headers")
                file(COPY "${_s}/vulkan" DESTINATION "${_iso}")
                if(EXISTS "${_s}/vk_video")
                    file(COPY "${_s}/vk_video" DESTINATION "${_iso}")
                endif()
                set(AUREA_VULKAN_INCLUDE "${_iso}")
                break()
            endif()
        endforeach()
    endif()
    if(AUREA_VULKAN_INCLUDE)
        set(AUREA_HAS_VULKAN ON)
        message(STATUS "Aurea: backend Vulkan no host (cabecalhos em ${AUREA_VULKAN_INCLUDE})")
    else()
        message(STATUS "Aurea: sem cabecalhos Vulkan no host — testes de GPU desligados")
    endif()
endif()

if(AUREA_HAS_VULKAN)
    add_library(aurea_vulkan STATIC
        gpu/vulkan/VulkanLoader.cpp
        gpu/vulkan/VulkanCommon.cpp
        gpu/vulkan/VulkanDevice.cpp
        gpu/vulkan/VulkanSwapchain.cpp
        gpu/vulkan/VulkanResources.cpp
        gpu/vulkan/VulkanCommands.cpp
    )
    target_include_directories(aurea_vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/gpu/vulkan)
    if(AUREA_VULKAN_INCLUDE)
        # SYSTEM: os avisos dos cabeçalhos do Khronos não são nossos.
        target_include_directories(aurea_vulkan SYSTEM PUBLIC "${AUREA_VULKAN_INCLUDE}")
    endif()
    target_link_libraries(aurea_vulkan PUBLIC aurea_core)
    target_compile_definitions(aurea_vulkan PUBLIC VK_NO_PROTOTYPES=1
        $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
    if(AUREA_PLATFORM_ANDROID)
        target_compile_definitions(aurea_vulkan PUBLIC VK_USE_PLATFORM_ANDROID_KHR=1)
        target_link_libraries(aurea_vulkan PUBLIC android dl)
    elseif(NOT WIN32)
        target_link_libraries(aurea_vulkan PUBLIC ${CMAKE_DL_LIBS})
    endif()
    target_compile_options(aurea_vulkan PRIVATE ${AUREA_WARNINGS}
        # `VkX info{VK_STRUCTURE_TYPE_X}` zera o resto de propósito (idioma do Vulkan).
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-missing-field-initializers>)
endif()

# -----------------------------------------------------------------------------
# aurea — a .so que o Android carrega. A ponte não entra no núcleo: o núcleo
# compila sem <jni.h>, senão deixa de rodar nos testes.
# -----------------------------------------------------------------------------
if(AUREA_PLATFORM_ANDROID)
    add_library(aurea SHARED
        platform/android/aurea_jni.cpp
        platform/android/MediaCodecSource.cpp
        platform/android/MediaCodecExport.cpp
        platform/android/AAudioOutput.cpp
    )
    target_include_directories(aurea PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/platform/android)
    # `mediandk` é o MediaCodec/MediaExtractor/ImageReader: sem ele não há
    # decode de hardware. `android` dá ANativeWindow e AHardwareBuffer.
    target_link_libraries(aurea PRIVATE aurea_core aurea_vulkan android log mediandk aaudio)
    target_compile_options(aurea PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -fno-exceptions>)
    set_target_properties(aurea PROPERTIES OUTPUT_NAME "aurea" PREFIX "lib")
endif()

# -----------------------------------------------------------------------------
# Testes (host)
# -----------------------------------------------------------------------------
if(AUREA_BUILD_TESTS AND NOT ANDROID AND NOT AUREA_PLATFORM_IOS)
    enable_testing()
    add_subdirectory(tests)
endif()
