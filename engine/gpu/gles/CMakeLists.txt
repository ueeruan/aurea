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
find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(GLOB_RECURSE _shader_inputs CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/shaders/*.vert" "${PROJECT_SOURCE_DIR}/shaders/*.frag" "${PROJECT_SOURCE_DIR}/shaders/*.comp")
list(SORT _shader_inputs)
set(_essl_files "")
foreach(_shader IN LISTS _shader_inputs)
    file(RELATIVE_PATH _relative "${PROJECT_SOURCE_DIR}/shaders" "${_shader}")
    set(_spv "${AUREA_SHADER_SPV_DIR}/${_relative}.spv")
    set(_essl "${_spv}.gles")
    add_custom_command(OUTPUT "${_essl}"
        COMMAND "${AUREA_GLES_COMPILER}" "${_spv}" "${_essl}"
        DEPENDS "${_spv}" "${AUREA_GLES_COMPILER}" VERBATIM)
    list(APPEND _essl_files "${_essl}")
endforeach()
set(_blob "${CMAKE_CURRENT_BINARY_DIR}/GlesShaderBlobs.cpp")
add_custom_command(OUTPUT "${_blob}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/gles-shaders/embed.py" --output "${_blob}" ${_essl_files}
    DEPENDS ${_essl_files} "${PROJECT_SOURCE_DIR}/tools/gles-shaders/embed.py" VERBATIM)
add_library(aurea_gles STATIC GlesDevice.cpp GlesResources.cpp GlesCommands.cpp "${_blob}")
target_include_directories(aurea_gles PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(aurea_gles PUBLIC aurea_core EGL GLESv3)
target_compile_options(aurea_gles PRIVATE -Wall -Wextra -fno-exceptions)
add_dependencies(aurea_gles aurea_core)

if(AUREA_ANDROID_GLES_PROBE)
    add_executable(aurea_gles_tests ../../tests/main.cpp ../../tests/test_gpu.cpp ../../tests/test_export.cpp
        ../../tests/test_ai.cpp ../../tests/test_upscale_color.cpp
        ../../tests/test_gles_surface.cpp ../../tests/test_proxy.cpp ../../platform/android/MediaCodecSource.cpp
        ../../platform/android/MediaCodecExport.cpp)
    target_include_directories(aurea_gles_tests PRIVATE ../../platform/android)
    target_link_libraries(aurea_gles_tests PRIVATE aurea_gles android mediandk log)
    target_compile_options(aurea_gles_tests PRIVATE -fno-rtti -fno-exceptions)
    target_compile_definitions(aurea_gles_tests PRIVATE AUREA_TEST_VULKAN=1 AUREA_TEST_GLES=1
        AUREA_GOLDEN_DIR="/data/local/tmp/aurea-gles-data/golden"
        AUREA_TEST_DATA_DIR="/data/local/tmp/aurea-gles-data/data")
endif()
