if(NOT ANDROID)
    return()
endif()
if(NOT AUREA_GLES_COMPILER)
    # Build a host executable separately from the NDK toolchain. A shared lock
    # prevents simultaneous ABI configurations from changing the same build.
    function(aurea_prepare_gles_compiler)
        set(_host_dir "${PROJECT_SOURCE_DIR}/build/shader-compilers")
        file(MAKE_DIRECTORY "${_host_dir}")
        file(LOCK "${_host_dir}/configure.lock" GUARD FUNCTION TIMEOUT 600)
        find_program(_host_cmake NAMES cmake REQUIRED NO_CMAKE_FIND_ROOT_PATH)
        execute_process(COMMAND "${_host_cmake}" -S "${PROJECT_SOURCE_DIR}/tools/metal-shaders"
            -B "${_host_dir}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=
            RESULT_VARIABLE _configured)
        if(NOT _configured EQUAL 0)
            message(FATAL_ERROR "Unable to configure the host ESSL compiler; set AUREA_GLES_COMPILER to a prebuilt host tool")
        endif()
        execute_process(COMMAND "${_host_cmake}" --build "${_host_dir}" --config Release
            --target aurea-gles-compiler --parallel 4 RESULT_VARIABLE _built)
        if(NOT _built EQUAL 0)
            message(FATAL_ERROR "Unable to build the host ESSL compiler")
        endif()
        find_program(_compiler NAMES aurea-gles-compiler
            PATHS "${_host_dir}/Release" "${_host_dir}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)
        set(AUREA_GLES_COMPILER "${_compiler}" PARENT_SCOPE)
    endfunction()
    aurea_prepare_gles_compiler()
elseif(NOT EXISTS "${AUREA_GLES_COMPILER}")
    message(FATAL_ERROR "AUREA_GLES_COMPILER does not exist: ${AUREA_GLES_COMPILER}")
endif()
execute_process(COMMAND "${AUREA_GLES_COMPILER}" --supports-shared-spv-key
    RESULT_VARIABLE _supports_shared_key TIMEOUT 10)
if(NOT _supports_shared_key EQUAL 0)
    message(FATAL_ERROR "Stale AUREA_GLES_COMPILER: rebuild engine/tools/metal-shaders target aurea-gles-compiler, or unset the prebuilt path to auto-build the current tool")
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(GLOB_RECURSE _shader_inputs CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/shaders/*.vert" "${PROJECT_SOURCE_DIR}/shaders/*.frag" "${PROJECT_SOURCE_DIR}/shaders/*.comp")
list(SORT _shader_inputs)
file(GLOB_RECURSE _shader_includes CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/shaders/*.glsl")
set(_essl_files "")
foreach(_shader IN LISTS _shader_inputs)
    file(RELATIVE_PATH _relative "${PROJECT_SOURCE_DIR}/shaders" "${_shader}")
    set(_spv "${AUREA_SHADER_SPV_DIR}/${_relative}.spv")
    set(_essl "${_spv}.gles")
    if(_relative STREQUAL "shape/shape.frag")
        # Adreno 610 physically SIGSEGVs in glCompileShader on the optimized
        # expanded shape CFG (all forms). The same complete source translated
        # with function boundaries preserved compiles on that driver. Use -O0
        # only for this ESSL translation; Vulkan/Metal keep the shared -O SPIR-V.
        aurea_find_glslc(_gles_glslc)
        set(_gles_source_spv "${_spv}.gles-source.spv")
        add_custom_command(OUTPUT "${_gles_source_spv}"
            COMMAND "${_gles_glslc}" --target-env=vulkan1.0 -O0 -Werror
                -I "${PROJECT_SOURCE_DIR}/shaders" -o "${_gles_source_spv}" "${_shader}"
            DEPENDS "${_shader}" ${_shader_includes} VERBATIM)
        add_custom_command(OUTPUT "${_essl}"
            COMMAND "${AUREA_GLES_COMPILER}" "${_gles_source_spv}" "${_essl}" "${_spv}"
            DEPENDS "${_gles_source_spv}" "${_spv}" "${AUREA_GLES_COMPILER}" VERBATIM)
    else()
        add_custom_command(OUTPUT "${_essl}"
            COMMAND "${AUREA_GLES_COMPILER}" "${_spv}" "${_essl}"
            DEPENDS "${_spv}" "${AUREA_GLES_COMPILER}" VERBATIM)
    endif()
    list(APPEND _essl_files "${_essl}")
endforeach()
set(_blob "${CMAKE_CURRENT_BINARY_DIR}/GlesShaderBlobs.cpp")
# Lista em arquivo de resposta: na linha de comando ela passa de 32 767
# caracteres (limite do CreateProcess) num worktree de caminho longo.
# file(GENERATE) só regrava quando a lista muda.
set(_essl_list "${CMAKE_CURRENT_BINARY_DIR}/GlesShaderBlobs.rsp")
list(JOIN _essl_files "\n" _essl_lines)
file(GENERATE OUTPUT "${_essl_list}" CONTENT "${_essl_lines}\n")
add_custom_command(OUTPUT "${_blob}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/gles-shaders/embed.py" --output "${_blob}" "@${_essl_list}"
    DEPENDS ${_essl_files} "${_essl_list}" "${PROJECT_SOURCE_DIR}/tools/gles-shaders/embed.py" VERBATIM)
add_library(aurea_gles STATIC GlesDevice.cpp GlesResources.cpp GlesCommands.cpp "${_blob}")
target_include_directories(aurea_gles PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(aurea_gles PUBLIC aurea_core EGL GLESv3)
target_compile_options(aurea_gles PRIVATE -Wall -Wextra -fno-exceptions)
add_dependencies(aurea_gles aurea_core)

if(AUREA_ANDROID_GLES_PROBE)
    add_executable(aurea_gles_tests ../../tests/main.cpp ../../tests/test_gpu.cpp ../../tests/test_export.cpp
        ../../tests/test_ai.cpp ../../tests/test_upscale_color.cpp
        ../../tests/test_gles_surface.cpp ../../tests/test_proxy.cpp ../../platform/android/MediaCodecSource.cpp)
    target_include_directories(aurea_gles_tests PRIVATE ../../platform/android)
    target_link_libraries(aurea_gles_tests PRIVATE aurea_gles android mediandk log)
    target_compile_options(aurea_gles_tests PRIVATE -fno-rtti -fno-exceptions)
    target_compile_definitions(aurea_gles_tests PRIVATE AUREA_TEST_VULKAN=1 AUREA_TEST_GLES=1
        AUREA_GOLDEN_DIR="/data/local/tmp/aurea-gles-data/golden"
        AUREA_TEST_DATA_DIR="/data/local/tmp/aurea-gles-data/data")
endif()
