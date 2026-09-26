include(FetchContent)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_METAL OFF CACHE BOOL "" FORCE)
set(GGML_BLAS OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
# Conservative two-thread CPU inference also works on ARMv7. No server fallback.
FetchContent_Declare(aurea_whisper
    URL https://github.com/ggml-org/whisper.cpp/archive/2eeeba56e9edd762b4b38467bab96c2517163158.tar.gz)
FetchContent_MakeAvailable(aurea_whisper)
# Android's application flags disable C++ exceptions globally. Whisper catches
# backend allocation errors internally; enable them only in its own C++ targets.
foreach(_whisper_target whisper ggml ggml-base ggml-cpu)
    if(TARGET ${_whisper_target})
        target_compile_options(${_whisper_target} PRIVATE
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/EHsc>
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-fexceptions>)
    endif()
endforeach()
target_sources(aurea_core PRIVATE src/text/LocalWhisper.cpp)
target_link_libraries(aurea_core PRIVATE whisper)
if(MSVC)
    set_source_files_properties(src/text/LocalWhisper.cpp PROPERTIES COMPILE_OPTIONS /EHsc)
else()
    set_source_files_properties(src/text/LocalWhisper.cpp PROPERTIES COMPILE_OPTIONS -fexceptions)
endif()
