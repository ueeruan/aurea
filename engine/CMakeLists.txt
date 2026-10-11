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
        LANGUAGES C CXX)   # C: ufbx.c

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

# ASan no host Windows (§136): o MSVC tem AddressSanitizer (não tem UBSan).
# Vale para TODOS os alvos (núcleo, Vulkan, terceiros e testes) — um alvo sem
# instrumentação no meio esconde a escrita fora do buffer que ele fizer.
# Build separado: engine/build/asan. Para rodar, a DLL do ASan
# (clang_rt.asan_dynamic-x86_64.dll, pasta bin do MSVC) precisa estar no PATH.
if(AUREA_ENABLE_SANITIZE AND MSVC)
    # /bigobj: com a instrumentação o harfbuzz.cc passa do limite de seções do .obj.
    add_compile_options(/fsanitize=address /Zi /bigobj)
    add_link_options(/INCREMENTAL:NO /DEBUG)
endif()

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
    src/core/MiniXml.cpp
    src/core/Thread.cpp
    src/ui/BuiltinPropertySchema.cpp
    src/memory/MemoryManager.cpp
    src/jobs/JobSystem.cpp
    src/command/History.cpp
    src/platform/DeviceCapabilities.cpp
    src/animation/Curve.cpp
    src/animation/Track.cpp
    src/animation/KeyframeOptimize.cpp
    src/timeline/Composition.cpp
    src/timeline/LayerAnimator.cpp
    src/timeline/Timeline.cpp
    src/timeline/Rig.cpp
    src/timeline/ShapeGeometry.cpp
    src/project/Project.cpp
    src/project/Serialization.cpp
    src/project/FileIO.cpp
    src/project/Presets.cpp
    src/project/AlightMotion.cpp
    src/project/Psd.cpp
    src/engine/EnginePsd.cpp
    src/engine/EngineGrid.cpp
    src/timeline/GridLayout.cpp
    src/effects/builtin/GridLayoutEffect.cpp
    src/project/ProjectPackage.cpp
    src/export/ImageEncode.cpp
    src/export/ExportRecovery.cpp
    src/export/ExportSinkStartup.cpp
    src/export/AureaExportEngineV2.cpp
    src/render/GPUCapabilities.cpp
    src/render/FrameGraph.cpp
    src/render/ShaderLibrary.cpp
    src/render/RenderScheduler.cpp
    src/render/Renderer.cpp
    src/render/MaskRaster.cpp
    src/render/ParticleExtras.cpp
    src/render/ParticleScene.cpp
    src/render/RendererDepth.cpp
    src/render/RendererRoto.cpp
    src/render/RendererAudio.cpp
    src/effects/Parameter.cpp
    src/effects/CubeLut.cpp
    src/effects/EffectGraph.cpp
    src/effects/builtin/ColorEffects.cpp
    src/effects/builtin/BlurEffects.cpp
    src/effects/builtin/GlowEffect.cpp
    src/effects/builtin/TransformEffect.cpp
    src/effects/builtin/MotionTileEffect.cpp
    src/effects/builtin/MotionDetectEffect.cpp
    src/effects/builtin/RetroDisplaceEffects.cpp
    src/effects/builtin/DatamoshEffect.cpp
    src/effects/builtin/KeyingEffects.cpp
    src/effects/builtin/ExpressionControls.cpp
    src/effects/builtin/EchoEffect.cpp
    src/effects/builtin/EffectsSupport.cpp
    src/effects/builtin/DistortEffects.cpp
    src/effects/builtin/SurfaceDeformEffects.cpp
    src/effects/builtin/PageTurnEffect.cpp
    src/effects/builtin/OpticalStylizeEffects.cpp
    src/effects/builtin/StylizeEffects.cpp
    src/effects/builtin/LightEffects.cpp
    src/effects/builtin/GlitchEffects.cpp
    src/effects/builtin/TemporalEffects.cpp
    src/effects/builtin/TransitionEffects.cpp
    src/effects/builtin/ProceduralWipeEffects.cpp
    src/effects/builtin/LensGradientEffect.cpp
    src/effects/builtin/PixelEncoderEffect.cpp
    src/effects/builtin/StarglowEffect.cpp
    src/effects/builtin/PixDitherEffect.cpp
    src/effects/builtin/VideoGlitchEffect.cpp
    src/effects/builtin/DisplaceTransformEffect.cpp
    src/effects/builtin/OpticalEffects.cpp
    src/effects/builtin/CornerPinEffect.cpp
    src/effects/builtin/MediaLabEffects.cpp
    src/effects/builtin/StudioLightEffects.cpp
    src/effects/builtin/TraceryEffect.cpp
    src/effects/builtin/PatternEffects.cpp
    src/effects/builtin/SampledBlurEffects.cpp
    src/effects/builtin/MotionBehaviorEffects.cpp
    src/effects/builtin/MotionExtras.cpp
    src/effects/builtin/RepeatExtras.cpp
    src/effects/builtin/MeshWarpEffect.cpp
    src/effects/Puppet.cpp
    src/effects/builtin/PuppetEffect.cpp
    src/effects/builtin/KeyingExtras.cpp
    src/effects/builtin/FinishingEffects.cpp
    src/effects/builtin/GenerateEffects.cpp
    src/effects/builtin/MatteEffects.cpp
    src/effects/builtin/VhsLookEffect.cpp
    src/effects/builtin/DepthEffects.cpp
    src/effects/builtin/RotobrushEffect.cpp
    src/effects/builtin/AudioPackEffects.cpp
    src/effects/builtin/ParticularEffect.cpp
    src/effects/builtin/DisintegrateEffect.cpp
    src/expr/Expression.cpp
    src/media/DecodedFrameCache.cpp
    src/media/YuvLayout.cpp
    src/media/P010Upload.cpp
    src/media/VideoSource.cpp
    src/media/MediaManager.cpp
    src/media/ThumbnailService.cpp
    src/media/PreviewProxy.cpp
    src/ai/DepthMapService.cpp
    src/ai/RotoMatte.cpp
    src/ai/RotoService.cpp
    src/audio/AudioCore.cpp
    src/audio/AudioBlockCache.cpp
    src/audio/AudioMixer.cpp
    src/audio/AudioFx.cpp
    src/audio/AudioEngine.cpp
    src/audio/Waveform.cpp
    src/audio/Beats.cpp
    src/audio/Spectrum.cpp
    src/tracking/PointTracker.cpp
    src/tracking/CameraTracker.cpp
    src/tracking/MotionGeometry.cpp
    src/text/Text.cpp
    src/text/FontManager.cpp
    src/text/TextAnimator.cpp
    src/text/TextTransform.cpp
    src/text/Captions.cpp
    src/engine/EngineCaptions.cpp
    src/engine/EngineCaptionPresets.cpp
    src/vector/Vector.cpp
    src/vector/Clipper.cpp
    src/vector/Svg.cpp
    src/vector/VectorDocument.cpp
    src/engine/EngineVector.cpp
    src/engine/EngineRig.cpp
    src/engine/EngineMeshWarp.cpp
    src/engine/EnginePuppet.cpp
    src/engine/EngineShape3D.cpp
    src/engine/EngineMedia.cpp
    src/engine/EngineLayerAnim.cpp
    src/engine/EngineRoto.cpp
    src/playback/Playback.cpp
    src/scene3d/GltfImporter.cpp
    src/scene3d/ModelBudget.cpp
    src/scene3d/SceneRenderer.cpp
    src/scene3d/GroundPlacement.cpp
    src/scene3d/Environment.cpp
    src/scene3d/EnvironmentJob.cpp
    src/scene3d/HdriImage.cpp
    src/scene3d/StudioEnvironment.cpp
    src/scene3d/Animation.cpp
    src/scene3d/Text3D.cpp
    src/scene3d/Shape3D.cpp
    src/scene3d/MaterialPreview.cpp
    src/engine/Engine.cpp
    src/engine/EngineMotion.cpp
    src/engine/EngineMotion.cpp
    ${AUREA_SHADER_SOURCES}
)

# -----------------------------------------------------------------------------
# Terceiros (third_party/README.md). Compilados à parte, com os avisos deles:
# o /W4 do motor não vale para código que não é nosso.
# -----------------------------------------------------------------------------
file(GLOB AUREA_MESHOPT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/meshoptimizer/src/*.cpp)
add_library(aurea_thirdparty STATIC ${AUREA_MESHOPT_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ufbx/ufbx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/basisu/transcoder/basisu_transcoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/basisu/zstd/zstddeclib.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/src/harfbuzz.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyexr/tinyexr.cc)
# KTX2 (ETC1S e UASTC, com Zstd) transcodificado para RGBA no import; os
# alvos de GPU que o motor não usa ficam fora (binário menor). PUBLIC: o
# cabeçalho do transcoder precisa ver a mesma configuração.
target_compile_definitions(aurea_thirdparty PUBLIC
    BASISD_SUPPORT_KTX2=1 BASISD_SUPPORT_KTX2_ZSTD=1
    BASISD_SUPPORT_PVRTC1=0 BASISD_SUPPORT_PVRTC2=0 BASISD_SUPPORT_ATC=0 BASISD_SUPPORT_FXT1=0
    BASISD_SUPPORT_ETC2_EAC_RG11=0 BASISD_SUPPORT_UASTC_HDR=0)
target_include_directories(aurea_thirdparty PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ufbx
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/meshoptimizer/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyexr
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/basisu/transcoder
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/src)
target_compile_options(aurea_thirdparty PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W0 /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w -fno-exceptions>
    $<$<AND:$<CONFIG:Release,RelWithDebInfo>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-O2>)
set_target_properties(aurea_thirdparty PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(aurea_core STATIC ${AUREA_CORE_SOURCES})
include(cmake/AureaNcnn.cmake)
include(cmake/AureaWhisper.cmake)
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
    # Keep the GL decoder bridge's color math byte-for-byte identical to the
    # shared shaders. Both aurea and aurea_media_probe inherit this include
    # directory through aurea_core. Regenerate when the source GLSL changes.
    set(_video_color_glsl "${CMAKE_CURRENT_SOURCE_DIR}/shaders/common/color.glsl")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_video_color_glsl}")
    file(READ "${_video_color_glsl}" AUREA_SHARED_COLOR_GLSL)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/VideoColorGlsl.hpp.in"
        "${AUREA_GENERATED_INCLUDE}/aurea/shaders/VideoColorGlsl.hpp" @ONLY)
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
# These import boundaries catch allocation failures and translate them to
# Status. Their allocating dependencies need unwind cleanup too, so a failed
# staging allocation releases local vectors and project snapshots on mobile.
set(AUREA_IMPORT_BOUNDARIES
    src/audio/Beats.cpp
    src/audio/AudioBlockCache.cpp
    src/engine/Engine.cpp
    src/engine/EngineMedia.cpp
    src/engine/EnginePsd.cpp
    src/engine/EngineRoto.cpp
    src/render/RendererRoto.cpp
    src/project/Psd.cpp
    src/project/Project.cpp
    src/command/History.cpp
    src/timeline/Composition.cpp
    src/timeline/Timeline.cpp
    src/scene3d/EnvironmentJob.cpp
)
list(APPEND AUREA_IMPORT_BOUNDARIES src/export/ExportRecovery.cpp src/export/ExportSinkStartup.cpp)
if(MSVC)
    set_source_files_properties(${AUREA_IMPORT_BOUNDARIES} PROPERTIES COMPILE_OPTIONS /EHsc)
else()
    set_source_files_properties(${AUREA_IMPORT_BOUNDARIES} PROPERTIES COMPILE_OPTIONS -fexceptions)
endif()
target_compile_options(aurea_core PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-O3>
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-O2 -g>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2>
)

if(AUREA_ENABLE_SANITIZE AND NOT MSVC AND NOT ANDROID AND NOT AUREA_PLATFORM_IOS)
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
    elseif(WIN32)
        target_compile_definitions(aurea_vulkan PUBLIC VK_USE_PLATFORM_WIN32_KHR=1 NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(aurea_vulkan PUBLIC user32)
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
    option(AUREA_ANDROID_GLES_PROBE "Build the on-device ES shader regression executable" OFF)
    if(AUREA_ANDROID_GLES_PROBE)
        add_executable(aurea_gles_probe tests/android_gles_probe.cpp)
        target_link_libraries(aurea_gles_probe PRIVATE EGL GLESv3)
    endif()
    option(AUREA_GPU_GLES "Build the OpenGL ES fallback backend" ON)
    if(AUREA_GPU_GLES)
        add_subdirectory(gpu/gles)
    endif()
    option(AUREA_ANDROID_MEDIA_PROBE "Build the on-device decoder regression executable" OFF)
    if(AUREA_ANDROID_MEDIA_PROBE)
        add_executable(aurea_media_probe tests/android_media_probe.cpp platform/android/MediaCodecSource.cpp
                       platform/android/GlVideoBridge.cpp)
        target_include_directories(aurea_media_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/platform/android)
        target_link_libraries(aurea_media_probe PRIVATE aurea_core android log mediandk EGL GLESv3)
        add_executable(aurea_gl_video_color_probe tests/android_gl_video_color_probe.cpp
                       platform/android/GlVideoBridge.cpp)
        target_include_directories(aurea_gl_video_color_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/platform/android)
        target_link_libraries(aurea_gl_video_color_probe PRIVATE aurea_core android log dl EGL GLESv3)
        add_executable(aurea_export_probe tests/android_export_probe.cpp platform/android/MediaCodecExport.cpp)
        target_include_directories(aurea_export_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/platform/android)
        target_link_libraries(aurea_export_probe PRIVATE aurea_core android log mediandk EGL GLESv3)
        add_executable(aurea_encoder_eos_probe tests/android_encoder_eos_probe.cpp
                       platform/android/MediaCodecExport.cpp)
        target_include_directories(aurea_encoder_eos_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/platform/android)
        target_link_libraries(aurea_encoder_eos_probe PRIVATE aurea_core android log mediandk EGL GLESv3)
        target_link_options(aurea_encoder_eos_probe PRIVATE
            -Wl,--wrap=AMediaCodec_dequeueOutputBuffer,--wrap=fsync,--wrap=close)
    endif()
    add_library(aurea SHARED
        platform/android/aurea_jni.cpp
        platform/android/MediaCodecSource.cpp
        platform/android/GlVideoBridge.cpp
        platform/android/MediaCodecExport.cpp
        platform/android/AAudioOutput.cpp
    )
    target_include_directories(aurea PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/platform/android)
    # `mediandk` é o MediaCodec/MediaExtractor/ImageReader: sem ele não há
    # decode de hardware. `android` dá ANativeWindow e AHardwareBuffer.
    # EGL + GLESv3: amostragem YUV explícita no caminho GL do vídeo.
    target_link_libraries(aurea PRIVATE aurea_core aurea_vulkan android log mediandk aaudio EGL GLESv3)
    if(AUREA_GPU_GLES)
        target_link_libraries(aurea PRIVATE aurea_gles)
        target_compile_definitions(aurea PRIVATE AUREA_GPU_GLES=1)
    endif()
    target_compile_options(aurea PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -fno-exceptions>)
    set_target_properties(aurea PROPERTIES OUTPUT_NAME "aurea" PREFIX "lib")
    # Páginas de 16 KB (exigência do Google Play para apps com alvo Android 15+):
    # os segmentos LOAD da .so alinhados em 16384. O NDK r28 já faz isso por
    # padrão no arm64; explícito para não depender do padrão do NDK e cobrir
    # também o armeabi-v7a.
    target_link_options(aurea PRIVATE -Wl,-z,max-page-size=16384)

    option(AUREA_ANDROID_SCENE_BENCHMARK "Build isolated on-device Vulkan/GLES scene performance probes" OFF)
    if(AUREA_ANDROID_SCENE_BENCHMARK)
        foreach(_backend IN ITEMS vulkan gles)
            if(_backend STREQUAL "gles" AND NOT AUREA_GPU_GLES)
                continue()
            endif()
            set(_probe_target "aurea_scene_${_backend}_probe")
            add_executable(${_probe_target} tests/android_scene_performance_probe.cpp)
            target_link_libraries(${_probe_target} PRIVATE aurea_${_backend} android log mediandk aaudio EGL GLESv3)
            target_compile_options(${_probe_target} PRIVATE -fno-rtti -fno-exceptions)
            if(_backend STREQUAL "vulkan")
                target_compile_definitions(${_probe_target} PRIVATE AUREA_PROBE_VULKAN=1)
            endif()
        endforeach()
    endif()

    option(AUREA_ANDROID_RUNTIME_TESTS "Build native Vulkan/GLES regression tests for a connected device" OFF)
    if(AUREA_ANDROID_RUNTIME_TESTS)
        set(_aurea_runtime_tests
            tests/main.cpp tests/test_gpu.cpp tests/test_motion_blur.cpp tests/test_render.cpp
            tests/test_media.cpp tests/test_engine.cpp tests/test_motion_geometry.cpp
            tests/test_export.cpp tests/test_export_scheduler.cpp
            tests/test_export_recovery.cpp tests/test_export_sink_startup.cpp tests/test_aac_output_integrity.cpp
            tests/test_scene_environment_jobs.cpp tests/test_morph_deformation.cpp
            tests/test_ground_placement.cpp tests/test_yuv_layout.cpp tests/test_p010_upload.cpp tests/test_memory.cpp
            tests/test_proxy.cpp platform/android/MediaCodecSource.cpp
            platform/android/GlVideoBridge.cpp platform/android/MediaCodecExport.cpp)
        foreach(_backend IN ITEMS vulkan gles)
            if(_backend STREQUAL "gles" AND NOT AUREA_GPU_GLES)
                continue()
            endif()
            set(_test_target "aurea_${_backend}_runtime_tests")
            add_executable(${_test_target} ${_aurea_runtime_tests})
            target_include_directories(${_test_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/platform/android)
            target_link_libraries(${_test_target} PRIVATE aurea_${_backend} android log mediandk aaudio EGL GLESv3)
            target_compile_options(${_test_target} PRIVATE -fno-rtti -fno-exceptions)
            target_compile_definitions(${_test_target} PRIVATE AUREA_TEST_VULKAN=1
                AUREA_GOLDEN_DIR="/data/local/tmp/aurea-codex/tests/golden"
                AUREA_TEST_DATA_DIR="/data/local/tmp/aurea-codex/tests/data"
                AUREA_APP_PRESETS_DIR="/data/local/tmp/aurea-codex/tests/presets")
            if(_backend STREQUAL "gles")
                target_compile_definitions(${_test_target} PRIVATE AUREA_TEST_GLES=1)
            endif()
        endforeach()
    endif()
endif()

# -----------------------------------------------------------------------------
# Testes (host)
# -----------------------------------------------------------------------------
if(AUREA_BUILD_TESTS AND NOT ANDROID AND NOT AUREA_PLATFORM_IOS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(WIN32 AND NOT AUREA_PLATFORM_ANDROID)
    option(AUREA_BUILD_DESKTOP "Build the Windows desktop editor" OFF)
    if(AUREA_BUILD_DESKTOP)
        add_subdirectory(platform/windows)
    endif()
endif()
