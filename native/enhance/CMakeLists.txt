# libaurea_enhance: IA de video sobre ncnn/Vulkan.
#   - super-resolucao (Real-ESRGAN, aurea_enhance.cpp)
#   - quadro intermediario para camera lenta (RIFE v4.6, aurea_rife.cpp)
# Os dois dividem o MESMO ncnn estatico e a mesma instancia Vulkan
# (aurea_gpu.h): duas bibliotecas duplicariam ~16 MB e brigariam pela GPU.
#
# ncnn vem do pacote OFICIAL 20260526, baixado uma vez e conferido por
# SHA256 (nada de binario no git). Offline: -DAUREA_NCNN_DIR=<pasta que
# contem lib/cmake/ncnn para a ABI>.
cmake_minimum_required(VERSION 3.22)

if(NOT AUREA_NCNN_DIR)
  if(ANDROID)
    set(_ver 20260526)
    set(_zip ${CMAKE_BINARY_DIR}/../ncnn-${_ver}-android-vulkan.zip)
    set(_root ${CMAKE_BINARY_DIR}/../ncnn-${_ver}-android-vulkan)
    file(LOCK ${CMAKE_BINARY_DIR}/../ncnn.lock GUARD FILE TIMEOUT 900)
    if(NOT EXISTS ${_root}/${ANDROID_ABI}/lib/cmake/ncnn/ncnnConfig.cmake)
      if(NOT EXISTS ${_zip})
        file(DOWNLOAD
          https://github.com/Tencent/ncnn/releases/download/${_ver}/ncnn-${_ver}-android-vulkan.zip
          ${_zip}
          EXPECTED_HASH SHA256=26909c92eed35afed4a966b5e9e503fcb0a529691ea3f910ec2c94a4fff52804
          TLS_VERIFY ON SHOW_PROGRESS)
      endif()
      file(ARCHIVE_EXTRACT INPUT ${_zip} DESTINATION ${CMAKE_BINARY_DIR}/..)
    endif()
    file(LOCK ${CMAKE_BINARY_DIR}/../ncnn.lock RELEASE)
    set(AUREA_NCNN_DIR ${_root}/${ANDROID_ABI})
  else()
    message(FATAL_ERROR "defina AUREA_NCNN_DIR (pacote ncnn com Vulkan da plataforma)")
  endif()
endif()

set(ncnn_DIR ${AUREA_NCNN_DIR}/lib/cmake/ncnn)
find_package(ncnn REQUIRED)

# RIFE (copia corrigida do rife-ncnn-vulkan, ver rife/PATCHES.md): os
# shaders .comp viram cabecalhos com o texto GLSL, compilado para SPIR-V
# pelo ncnn na primeira execucao.
set(_rife ${CMAKE_CURRENT_SOURCE_DIR}/rife)
set(_rife_gerados ${CMAKE_CURRENT_BINARY_DIR}/rife_shaders)
file(MAKE_DIRECTORY ${_rife_gerados})
set(_rife_shaders
  rife_preproc rife_postproc rife_preproc_tta rife_postproc_tta
  rife_flow_tta_avg rife_v2_flow_tta_avg rife_v4_flow_tta_avg
  rife_flow_tta_temporal_avg rife_v2_flow_tta_temporal_avg rife_v4_flow_tta_temporal_avg
  rife_out_tta_temporal_avg rife_v4_timestep rife_v4_timestep_tta
  warp warp_pack4 warp_pack8)
set(_rife_cabecalhos)
foreach(s ${_rife_shaders})
  set(h ${_rife_gerados}/${s}.comp.hex.h)
  add_custom_command(OUTPUT ${h}
    COMMAND ${CMAKE_COMMAND} -DSHADER_SRC=${_rife}/${s}.comp -DSHADER_COMP_HEADER=${h}
            -P ${_rife}/generate_shader_comp_header.cmake
    DEPENDS ${_rife}/${s}.comp ${_rife}/generate_shader_comp_header.cmake
    WORKING_DIRECTORY ${_rife_gerados}
    VERBATIM)
  list(APPEND _rife_cabecalhos ${h})
endforeach()

add_library(aurea_enhance SHARED
  aurea_enhance.cpp
  aurea_rife.cpp
  ${_rife}/rife.cpp
  ${_rife}/warp.cpp
  ${_rife_cabecalhos})
target_compile_features(aurea_enhance PRIVATE cxx_std_17)
target_include_directories(aurea_enhance PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${_rife}
  ${_rife_gerados}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools)  # stb_image / stb_image_write
target_link_libraries(aurea_enhance PRIVATE ncnn)
if(MSVC)
  target_compile_definitions(aurea_enhance PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(ANDROID)
  # OpenMP do NDK embutido: sem libomp.so solta no APK. zlib e do sistema.
  target_link_options(aurea_enhance PRIVATE -static-openmp -fopenmp)
  target_link_libraries(aurea_enhance PRIVATE z)
endif()
