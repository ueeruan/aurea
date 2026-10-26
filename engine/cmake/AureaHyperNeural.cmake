# =============================================================================
#  AureaHyper — as redes Lite/Max (src/hyper/neural/**).
#
#  Os fontes entram pelo GLOB do CMakeLists.txt. Aqui: os modelos embutidos
#  (engine/assets/hyper, receita em tools/hyper_models.py; licenças e hashes
#  nos manifest.json) e as exceções que a fronteira do ncnn traduz em Status.
#  Incluído por cmake/AureaNcnn.cmake (depois do alvo ncnn existir).
# =============================================================================
set(hyper_asset_root "${CMAKE_CURRENT_SOURCE_DIR}/assets/hyper")
# Embutidos no app (~18 MB). O GFPGAN (178 MB) é sob demanda e fica fora.
set(AUREA_HYPER_ASSETS
    span-x2/span-x2-fp16.param span-x2/span-x2-fp16.bin
    span-x2/reference-32-x2.rgb
    span-x4/span-x4-fp16.param span-x4/span-x4-fp16.bin
    span-x4/reference-32-x4.rgb
    realesr-general-x4v3/srvgg-x4.param realesr-general-x4v3/x4v3.bin16 realesr-general-x4v3/wdn-x4v3.bin16
    realesr-general-x4v3/reference-dn50-32-x4.rgb
    realesr-animevideov3/animevideov3-x4-int8.param realesr-animevideov3/animevideov3-x4-int8.bin
    rife-v4.6/flownet.param rife-v4.6/flownet.bin)
set(hyper_asset_paths "")
foreach(rel IN LISTS AUREA_HYPER_ASSETS)
    list(APPEND hyper_asset_paths "${hyper_asset_root}/${rel}")
endforeach()
set(hyper_assets_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/HyperAssets.cpp")
if(MSVC)
    string(REPLACE ";" "|" hyper_asset_list "${AUREA_HYPER_ASSETS}")
    add_custom_command(OUTPUT "${hyper_assets_cpp}"
        COMMAND "${CMAKE_COMMAND}" "-DROOT=${hyper_asset_root}" "-DFILES=${hyper_asset_list}" "-DOUTPUT=${hyper_assets_cpp}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedHyperAssets.cmake"
        DEPENDS ${hyper_asset_paths} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedHyperAssets.cmake"
        VERBATIM)
else()
    # Clang/GCC (Android, iOS): os bytes entram no objeto pelo `.incbin` do
    # montador, sem array C++ gigante (como cmake/AiDepthModel.incbin.cpp.in).
    set(AUREA_HYPER_ASM "")
    set(AUREA_HYPER_DECL "")
    set(AUREA_HYPER_TABLE "")
    set(index 0)
    foreach(rel IN LISTS AUREA_HYPER_ASSETS)
        set(sym "aurea_hyper_asset_${index}")
        string(APPEND AUREA_HYPER_ASM "    \".p2align 4\\n\" AUREA_HYPER_LABEL(\"${sym}\") \".incbin \\\"${hyper_asset_root}/${rel}\\\"\\n\" AUREA_HYPER_LABEL(\"${sym}_end\") \".byte 0\\n\"\n")
        string(APPEND AUREA_HYPER_DECL "extern \"C\" const unsigned char ${sym}[];\nextern \"C\" const unsigned char ${sym}_end[];\n")
        string(APPEND AUREA_HYPER_TABLE "    {\"${rel}\", ${sym}, ${sym}_end},\n")
        math(EXPR index "${index} + 1")
    endforeach()
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/HyperAssets.incbin.cpp.in" "${hyper_assets_cpp}" @ONLY)
    # O scan de dependências do compilador não vê as entradas do .incbin.
    set_source_files_properties("${hyper_assets_cpp}" PROPERTIES OBJECT_DEPENDS "${hyper_asset_paths}")
endif()
target_sources(aurea_core PRIVATE "${hyper_assets_cpp}")
# A fronteira com o ncnn (alocação de vetores/modelos) traduz exceções em
# Status, como src/ai/*.cpp; o resto do núcleo segue sem exceções.
file(GLOB_RECURSE hyper_neural_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/hyper/neural/*.cpp)
if(MSVC)
    set_source_files_properties(${hyper_neural_sources} PROPERTIES COMPILE_OPTIONS /EHsc)
else()
    set_source_files_properties(${hyper_neural_sources} PROPERTIES COMPILE_OPTIONS -fexceptions)
endif()
