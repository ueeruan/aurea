# libaurea_enhance: super-resolucao por IA (ncnn/Vulkan).
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

add_library(aurea_enhance SHARED aurea_enhance.cpp)
target_compile_features(aurea_enhance PRIVATE cxx_std_17)
target_link_libraries(aurea_enhance PRIVATE ncnn)
if(ANDROID)
  # OpenMP do NDK embutido: sem libomp.so solta no APK.
  target_link_options(aurea_enhance PRIVATE -static-openmp -fopenmp)
endif()
