# =============================================================================
#  Aurea PC — aurea.dll para a interface Compose Desktop (JVM).
#
#  A MESMA ponte JNI do Android (platform/android/aurea_jni.cpp, sem fork):
#  a JVM faz System.loadLibrary("aurea") e chama as mesmas funções de
#  com.aurea.aurea.engine.AureaEngine. O que muda no PC fica atrás de
#  `#if defined(__ANDROID__)` naquele arquivo e em DesktopJniPlatform.hpp.
#
#  A prévia não vai para uma janela: o backend Vulkan desenha num anel de
#  imagens próprio e copia o quadro para buffers lidos pela UI
#  (gpu/vulkan/VulkanHostPresent.cpp, AUREA_VK_HOST_PRESENT) — a UI desenha
#  por cima do palco. Build: tools/build_aurea_pc_jni.cmd.
# =============================================================================
if(NOT TARGET aurea_vulkan)
    message(FATAL_ERROR "aurea.dll (Aurea PC) precisa dos cabecalhos Vulkan no build e de um driver Vulkan no uso")
endif()

set(AUREA_JDK_HOME "D:/Program Files/Eclipse Adoptium/jdk-21.0.12.101-hotspot" CACHE PATH "JDK com include/jni.h")
if(NOT EXISTS "${AUREA_JDK_HOME}/include/jni.h" AND DEFINED ENV{JAVA_HOME})
    file(TO_CMAKE_PATH "$ENV{JAVA_HOME}" _aurea_java_home)
    set(AUREA_JDK_HOME "${_aurea_java_home}" CACHE PATH "JDK com include/jni.h" FORCE)
endif()
if(NOT EXISTS "${AUREA_JDK_HOME}/include/jni.h")
    message(FATAL_ERROR "aurea.dll: jni.h nao encontrado em ${AUREA_JDK_HOME}/include (defina AUREA_JDK_HOME)")
endif()

# Mídia do Windows (Media Foundation, waveOut, WIC). O editor nativo
# (platform/windows) define o mesmo alvo quando também é compilado.
if(NOT TARGET aurea_windows_media)
    add_library(aurea_windows_media STATIC ${CMAKE_CURRENT_SOURCE_DIR}/../windows/WindowsMedia.cpp)
    target_include_directories(aurea_windows_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../windows)
    target_link_libraries(aurea_windows_media PUBLIC aurea_core mfplat mfreadwrite mfuuid ole32 propsys windowscodecs winmm)
    target_compile_definitions(aurea_windows_media PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(aurea_windows_media PRIVATE /utf-8 /EHsc)
endif()

# Apresentação no host: só neste build (o layout do vk::Backend muda com ele).
target_sources(aurea_vulkan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../gpu/vulkan/VulkanHostPresent.cpp)
target_compile_definitions(aurea_vulkan PUBLIC AUREA_VK_HOST_PRESENT=1)

add_library(aurea SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/../android/aurea_jni.cpp
    DesktopFrames.cpp
    # Motion Capture do PC: vídeo (Media Foundation) → BlazePose do motor (src/ai/PoseEstimator.cpp).
    DesktopMocap.cpp)
target_include_directories(aurea PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../windows
    "${AUREA_JDK_HOME}/include"
    "${AUREA_JDK_HOME}/include/win32")
target_link_libraries(aurea PRIVATE aurea_core aurea_vulkan aurea_windows_media)
target_compile_definitions(aurea PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
# /bigobj: a ponte tem ~700 funções exportadas num arquivo só.
target_compile_options(aurea PRIVATE /utf-8 /EHsc /bigobj /permissive- /Zc:__cplusplus)
set_target_properties(aurea PROPERTIES OUTPUT_NAME "aurea" PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# As DLLs do FFmpeg (export) vão ao lado da aurea.dll: a pasta é a que a JVM
# recebe em java.library.path.
file(GLOB _aurea_ffmpeg_dlls "${AUREA_FFMPEG_ROOT}/bin/*.dll")
if(_aurea_ffmpeg_dlls)
    add_custom_command(TARGET aurea POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_aurea_ffmpeg_dlls} "$<TARGET_FILE_DIR:aurea>"
        VERBATIM)
endif()

# Teste de fumaça (não vai no app): uma JVM de verdade carrega a aurea.dll,
# anexa a apresentação no host e mede ritmo e custo parado.
add_executable(aurea_pc_smoke DesktopSmoke.cpp)
target_include_directories(aurea_pc_smoke PRIVATE "${AUREA_JDK_HOME}/include" "${AUREA_JDK_HOME}/include/win32")
# A mídia do Windows entra direto: o teste mede a saída de áudio e o decode.
target_link_libraries(aurea_pc_smoke PRIVATE aurea_core aurea_windows_media)
target_compile_definitions(aurea_pc_smoke PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
    "AUREA_SMOKE_JVM=L\"${AUREA_JDK_HOME}/bin/server/jvm.dll\"")
target_compile_options(aurea_pc_smoke PRIVATE /utf-8 /EHsc)
set_target_properties(aurea_pc_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_dependencies(aurea_pc_smoke aurea)

# Teste sem janela da prévia com um projeto real (vídeo parado, tocando e a
# captura da miniatura). Áudio em silêncio. Não vai no app.
add_executable(aurea_pc_video_test DesktopVideoTest.cpp)
target_include_directories(aurea_pc_video_test PRIVATE "${AUREA_JDK_HOME}/include" "${AUREA_JDK_HOME}/include/win32")
target_link_libraries(aurea_pc_video_test PRIVATE aurea_core)
target_compile_definitions(aurea_pc_video_test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
    "AUREA_SMOKE_JVM=L\"${AUREA_JDK_HOME}/bin/server/jvm.dll\"")
target_compile_options(aurea_pc_video_test PRIVATE /utf-8 /EHsc)
set_target_properties(aurea_pc_video_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_dependencies(aurea_pc_video_test aurea)

# Medida do decode de vídeo (MediaFactory direto, sem JVM nem áudio): ms por
# quadro, seeks e decoders em paralelo. Não vai no app.
add_executable(aurea_pc_decode_bench DesktopDecodeBench.cpp)
target_link_libraries(aurea_pc_decode_bench PRIVATE aurea_core aurea_windows_media)
target_compile_definitions(aurea_pc_decode_bench PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(aurea_pc_decode_bench PRIVATE /utf-8 /EHsc)
set_target_properties(aurea_pc_decode_bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Motion Capture de ponta a ponta (a mesma sessão da aurea.dll: Media Foundation
# → BlazePose → quadros do motor), sem JVM. Não vai no app.
add_executable(aurea_pc_mocap_bench DesktopMocapBench.cpp DesktopMocap.cpp)
target_include_directories(aurea_pc_mocap_bench PRIVATE "${AUREA_JDK_HOME}/include" "${AUREA_JDK_HOME}/include/win32")
target_link_libraries(aurea_pc_mocap_bench PRIVATE aurea_core aurea_windows_media)
target_compile_definitions(aurea_pc_mocap_bench PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(aurea_pc_mocap_bench PRIVATE /utf-8 /EHsc)
set_target_properties(aurea_pc_mocap_bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
