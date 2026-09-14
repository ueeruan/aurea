cmake_minimum_required(VERSION 3.22.1)
project(aurea_native VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Otimizações de desempenho e flags modernas
if (MSVC)
    add_compile_options(/W4 /O2 /permissive-)
else()
    add_compile_options(-Wall -Wextra -O3 -fvisibility=hidden)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/animation
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/effects
    ${CMAKE_CURRENT_SOURCE_DIR}/scene3d
    ${CMAKE_CURRENT_SOURCE_DIR}/video
    ${CMAKE_CURRENT_SOURCE_DIR}/video/time_remap
    ${CMAKE_CURRENT_SOURCE_DIR}/video/ffmpeg
    ${CMAKE_CURRENT_SOURCE_DIR}/video/cache
    ${CMAKE_CURRENT_SOURCE_DIR}/optical_flow
    ${CMAKE_CURRENT_SOURCE_DIR}/preview
    ${CMAKE_CURRENT_SOURCE_DIR}/export
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge
)

set(AUREA_SOURCES
    # Core
    core/ProjectCore.cpp
    core/TimelineCore.cpp
    core/CompositionCore.cpp

    # Animation
    animation/KeyframeEngine.cpp
    animation/MotionGraphEngine.cpp
    animation/AnimationEngine.cpp

    # Renderer
    renderer/RenderEngine.cpp
    renderer/GPUProcessor.cpp
    renderer/Renderer2D.cpp
    renderer/RenderTargetPool.cpp

    # Effects
    effects/EffectsEngine.cpp

    # Scene 3D
    scene3d/Scene3D.cpp
    scene3d/Camera.cpp
    scene3d/Materials.cpp
    scene3d/Assets.cpp
    scene3d/JobSystem.cpp
    scene3d/DeviceProfile.cpp
    scene3d/ModelAnalyzer.cpp
    scene3d/GPUUploadQueue.cpp
    scene3d/TextureManager.cpp
    scene3d/LODManager.cpp
    scene3d/AssetCache.cpp
    scene3d/ModelImportManager.cpp
    scene3d/FilamentRenderer.cpp

    # Video & Time Remap Engine
    video/time_remap/BezierEvaluator.cpp
    video/time_remap/TimeRemapCurve.cpp
    video/time_remap/TimeRemapProperty.cpp
    video/ffmpeg/VideoTimeMapper.cpp
    video/ffmpeg/FFmpegVideoDecoder.cpp
    video/cache/VideoFrameCache.cpp
    video/cache/PrefetchManager.cpp
    video/VideoEngine.cpp

    # Optical Flow & RIFE Frame Interpolation (ncnn + Vulkan)
    optical_flow/SceneCutDetector.cpp
    optical_flow/FlowFrameCache.cpp
    optical_flow/RIFEModelManager.cpp
    optical_flow/VulkanBackend.cpp
    optical_flow/NCNNBackend.cpp
    optical_flow/QualityController.cpp
    optical_flow/InterpolationScheduler.cpp
    optical_flow/GPUTextureBridge.cpp
    optical_flow/OpticalFlowEngine.cpp

    # Preview & RenderCore
    preview/PreviewEngine.cpp
    preview/FrameScheduler.cpp
    rendercore/RenderCore.cpp   # async render core
    export/NativeExportPipeline.cpp

    # Bridge (C API para FFI)
    bridge/AureaNativeBridge.cpp
)

add_library(aurea_native SHARED ${AUREA_SOURCES})

# Configurações específicas por plataforma
if (ANDROID)
    find_library(LOG_LIB log)
    find_library(ANDROID_LIB android)
    find_library(EGL_LIB EGL)
    find_library(GLES_LIB GLESv3)
    find_library(MEDIA_LIB mediandk)
    find_library(VULKAN_LIB vulkan)

    target_link_libraries(aurea_native
        ${LOG_LIB}
        ${ANDROID_LIB}
        ${EGL_LIB}
        ${GLES_LIB}
        ${MEDIA_LIB}
        ${VULKAN_LIB}
    )
elseif (APPLE)
    find_library(METAL_LIB Metal)
    find_library(QUARTZ_CORE QuartzCore)
    find_library(CORE_VIDEO CoreVideo)
    find_library(AV_FOUNDATION AVFoundation)

    target_link_libraries(aurea_native
        ${METAL_LIB}
        ${QUARTZ_CORE}
        ${CORE_VIDEO}
        ${AV_FOUNDATION}
    )
endif()

# Super-resolucao por IA (Real-ESRGAN sobre ncnn/Vulkan): libaurea_enhance.so
if (ANDROID)
    add_subdirectory(enhance)
endif()
