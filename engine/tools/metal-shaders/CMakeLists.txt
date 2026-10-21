cmake_minimum_required(VERSION 3.22)
project(AureaMetalCompiler LANGUAGES CXX)
# A tradução (SPIRV-Cross fixado + ShaderTranslate.cpp) é a MESMA que os
# backends GLES e Metal usam em runtime para os shaders dos plugins.
set(AUREA_XLATE_MSL ON)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/AureaShaderXlate.cmake)
add_executable(aurea-metal-compiler main.cpp)
target_compile_features(aurea-metal-compiler PRIVATE cxx_std_17)
target_link_libraries(aurea-metal-compiler PRIVATE aurea_shader_xlate)
add_executable(aurea-gles-compiler ../gles-shaders/main.cpp)
target_compile_features(aurea-gles-compiler PRIVATE cxx_std_17)
target_link_libraries(aurea-gles-compiler PRIVATE aurea_shader_xlate)
