# =============================================================================
#  AureaShaders.cmake — GLSL → SPIR-V no build.
#
#  Os shaders vivem em `engine/shaders/{common,video,effects,composite}` como
#  arquivos GLSL de verdade (nada de string gigante em C++). No build:
#
#    1. cada .vert/.frag/.comp vira .spv com o glslc (o do NDK, o mesmo que o
#       Android usa — um compilador só para host e aparelho);
#    2. os .spv são embutidos num .cpp gerado, junto de um enum `ShaderId`.
#
#  O enum é gerado na CONFIGURAÇÃO (depende só da lista de arquivos): um efeito
#  que referencia um shader que não existe é erro de COMPILAÇÃO, não um pipeline
#  que falha em runtime no aparelho do usuário.
#
#  O iOS (próxima fase) recebe MSL gerado destes mesmos .spv pelo SPIRV-Cross,
#  também no build — preview e export seguem com uma única fonte.
# =============================================================================

function(aurea_find_glslc out_var)
    if(AUREA_GLSLC AND EXISTS "${AUREA_GLSLC}")
        set(${out_var} "${AUREA_GLSLC}" PARENT_SCOPE)
        return()
    endif()

    if(CMAKE_HOST_WIN32)
        set(_tag "windows-x86_64")
        set(_exe "glslc.exe")
    elseif(CMAKE_HOST_APPLE)
        set(_tag "darwin-x86_64")
        set(_exe "glslc")
    else()
        set(_tag "linux-x86_64")
        set(_exe "glslc")
    endif()

    set(_candidates "")
    if(ANDROID_NDK)
        list(APPEND _candidates "${ANDROID_NDK}/shader-tools/${_tag}/${_exe}")
    endif()
    if(CMAKE_ANDROID_NDK)
        list(APPEND _candidates "${CMAKE_ANDROID_NDK}/shader-tools/${_tag}/${_exe}")
    endif()
    if(DEFINED ENV{VULKAN_SDK})
        list(APPEND _candidates "$ENV{VULKAN_SDK}/bin/${_exe}" "$ENV{VULKAN_SDK}/Bin/${_exe}")
    endif()
    if(DEFINED ENV{ANDROID_NDK_HOME})
        list(APPEND _candidates "$ENV{ANDROID_NDK_HOME}/shader-tools/${_tag}/${_exe}")
    endif()

    # Host de desenvolvimento: o NDK declarado em android/local.properties. É o
    # mesmo glslc do build do APK — sem instalar Vulkan SDK só para os testes.
    set(_props "${CMAKE_CURRENT_LIST_DIR}/../android/local.properties")
    if(NOT EXISTS "${_props}")
        set(_props "${PROJECT_SOURCE_DIR}/../android/local.properties")
    endif()
    if(EXISTS "${_props}")
        file(STRINGS "${_props}" _lines)
        foreach(_line IN LISTS _lines)
            if(_line MATCHES "^ndk\\.dir=(.*)$")
                list(APPEND _candidates "${CMAKE_MATCH_1}/shader-tools/${_tag}/${_exe}")
            elseif(_line MATCHES "^sdk\\.dir=(.*)$")
                file(GLOB _ndks "${CMAKE_MATCH_1}/ndk/*")
                foreach(_n IN LISTS _ndks)
                    list(APPEND _candidates "${_n}/shader-tools/${_tag}/${_exe}")
                endforeach()
            endif()
        endforeach()
    endif()

    foreach(_c IN LISTS _candidates)
        string(REPLACE "\\:" ":" _c "${_c}")
        string(REPLACE "\\\\" "/" _c "${_c}")
        if(EXISTS "${_c}")
            set(${out_var} "${_c}" PARENT_SCOPE)
            set(AUREA_GLSLC "${_c}" CACHE FILEPATH "glslc usado para os shaders" FORCE)
            return()
        endif()
    endforeach()

    find_program(_path_glslc glslc)
    if(_path_glslc)
        set(${out_var} "${_path_glslc}" PARENT_SCOPE)
        return()
    endif()

    message(FATAL_ERROR
        "glslc nao encontrado. Os shaders do Aurea sao compilados no build.\n"
        "Defina -DAUREA_GLSLC=<caminho>, VULKAN_SDK, ou android/local.properties com ndk.dir.")
endfunction()

# aurea_compile_shaders(<out_sources_var> <out_include_dir_var>)
function(aurea_compile_shaders out_sources out_include_dir)
    aurea_find_glslc(_glslc)

    set(_src_root "${PROJECT_SOURCE_DIR}/shaders")
    set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
    set(_inc_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
    file(MAKE_DIRECTORY "${_gen_dir}" "${_inc_dir}/aurea/shaders")

    file(GLOB_RECURSE _shaders CONFIGURE_DEPENDS
        "${_src_root}/*.vert" "${_src_root}/*.frag" "${_src_root}/*.comp")
    list(SORT _shaders)
    file(GLOB_RECURSE _includes CONFIGURE_DEPENDS "${_src_root}/*.glsl")

    set(_spv_list "")
    set(_enum_body "")
    set(_name_body "")
    set(_stage_body "")
    foreach(_s IN LISTS _shaders)
        file(RELATIVE_PATH _rel "${_src_root}" "${_s}")
        string(REGEX REPLACE "[/\\.]" "_" _id "${_rel}")
        set(_out "${_gen_dir}/${_rel}.spv")
        get_filename_component(_out_dir "${_out}" DIRECTORY)
        file(MAKE_DIRECTORY "${_out_dir}")

        # -O: o otimizador do SPIR-V roda no build, não no driver do aparelho.
        # SPIR-V 1.0 works on both Vulkan 1.0 and newer drivers. Zero-copy
        # video remains enabled separately when Vulkan 1.1 supports it.
        add_custom_command(
            OUTPUT "${_out}"
            COMMAND "${_glslc}" --target-env=vulkan1.0 -O -Werror
                    -I "${_src_root}" -o "${_out}" "${_s}"
            DEPENDS "${_s}" ${_includes}
            COMMENT "glslc ${_rel}"
            VERBATIM)
        list(APPEND _spv_list "${_out}")

        string(APPEND _enum_body "    ${_id},\n")
        string(APPEND _name_body "    \"${_rel}\",\n")
        if(_rel MATCHES "\\.vert$")
            string(APPEND _stage_body "    ShaderStage::Vertex,\n")
        elseif(_rel MATCHES "\\.frag$")
            string(APPEND _stage_body "    ShaderStage::Fragment,\n")
        else()
            string(APPEND _stage_body "    ShaderStage::Compute,\n")
        endif()
    endforeach()

    list(LENGTH _shaders _count)

    # Cabeçalho com o enum — gerado na configuração, reescrito só se mudar
    # (senão cada reconfiguração recompilaria o motor inteiro).
    set(_hdr_tmp "${_gen_dir}/ShaderIds.hpp.tmp")
    file(WRITE "${_hdr_tmp}"
"// GERADO por cmake/AureaShaders.cmake — nao editar.
#pragma once
#include \"aurea/render/GPUBackend.hpp\"

namespace aurea {

enum class ShaderId : u16 {
${_enum_body}    Count
};

inline constexpr u32 kShaderCount = ${_count};

inline constexpr const char* kShaderNames[kShaderCount] = {
${_name_body}};

inline constexpr ShaderStage kShaderStages[kShaderCount] = {
${_stage_body}};

/// SPIR-V embutido, na ordem de `ShaderId`.
struct ShaderBlob {
    const u32* words = nullptr;
    usize bytes = 0;
};
const ShaderBlob& shader_blob(ShaderId id) noexcept;

} // namespace aurea
")
    configure_file("${_hdr_tmp}" "${_inc_dir}/aurea/shaders/ShaderIds.hpp" COPYONLY)

    set(_embed_list ${_spv_list})
    if(AUREA_METAL_SHADERS)
        if(NOT EXISTS "${AUREA_METAL_COMPILER}")
            message(FATAL_ERROR "Build engine/tools/metal-shaders for the HOST and set AUREA_METAL_COMPILER")
        endif()
        find_program(_xcrun xcrun REQUIRED)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        string(TOLOWER "${CMAKE_OSX_SYSROOT}" _metal_sysroot)
        set(_metal_minimum "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        if(AUREA_IOS_SIMULATOR OR _metal_sysroot MATCHES "iphonesimulator")
            set(_metal_sdk iphonesimulator)
            set(_metal_standard ios-metal2.1)
            if(NOT _metal_minimum)
                set(_metal_minimum 16.3)
            endif()
            set(_metal_minimum_flag "-miphonesimulator-version-min=${_metal_minimum}")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR _metal_sysroot MATCHES "iphoneos")
            set(_metal_sdk iphoneos)
            set(_metal_standard ios-metal2.1)
            if(NOT _metal_minimum)
                set(_metal_minimum 16.3)
            endif()
            set(_metal_minimum_flag "-mios-version-min=${_metal_minimum}")
        else()
            set(_metal_sdk macosx)
            set(_metal_standard macos-metal2.1)
            if(NOT _metal_minimum)
                set(_metal_minimum 11.0)
            endif()
            set(_metal_minimum_flag "-mmacos-version-min=${_metal_minimum}")
        endif()
        message(STATUS "Aurea Metal shaders: SDK ${_metal_sdk}, ${_metal_minimum_flag}")
        set(_embed_list "")
        foreach(_spv IN LISTS _spv_list)
            set(_raw "${_spv}.mslraw")
            set(_air "${_spv}.air")
            set(_metallib "${_spv}.metallib")
            set(_msl "${_spv}.mslblob")
            add_custom_command(OUTPUT "${_msl}"
                BYPRODUCTS "${_raw}" "${_raw}.metal" "${_air}" "${_metallib}"
                COMMAND "${AUREA_METAL_COMPILER}" "${_spv}" "${_raw}"
                COMMAND "${_xcrun}" --sdk "${_metal_sdk}" metal -c "-std=${_metal_standard}"
                        "${_metal_minimum_flag}"
                        "${_raw}.metal" -o "${_air}"
                COMMAND "${_xcrun}" --sdk "${_metal_sdk}" metallib "${_air}" -o "${_metallib}"
                COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/metal-shaders/pack_metallib.py"
                        "${_raw}" "${_metallib}" "${_msl}"
                DEPENDS "${_spv}" "${AUREA_METAL_COMPILER}"
                        "${PROJECT_SOURCE_DIR}/tools/metal-shaders/pack_metallib.py"
                COMMENT "SPIR-V -> Metal precompilado: ${_spv}"
                VERBATIM)
            list(APPEND _embed_list "${_msl}")
        endforeach()
    endif()

    set(_blob_cpp "${_gen_dir}/ShaderBlobs.cpp")
    add_custom_command(
        OUTPUT "${_blob_cpp}"
        COMMAND ${CMAKE_COMMAND}
                "-DSPV_FILES=${_embed_list}"
                "-DOUT_CPP=${_blob_cpp}"
                -P "${PROJECT_SOURCE_DIR}/cmake/embed_shaders.cmake"
        DEPENDS ${_embed_list} "${PROJECT_SOURCE_DIR}/cmake/embed_shaders.cmake"
        COMMENT "Embutindo ${_count} shaders SPIR-V"
        VERBATIM)

    set(${out_sources} "${_blob_cpp}" PARENT_SCOPE)
    set(${out_include_dir} "${_inc_dir}" PARENT_SCOPE)
    set(AUREA_SHADER_SPV_DIR "${_gen_dir}" PARENT_SCOPE)
endfunction()
