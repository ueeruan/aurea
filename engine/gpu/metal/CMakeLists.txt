# =============================================================================
#  aurea_metal — o backend Metal do Aurea (iOS; e macOS, para o host de
#  desenvolvimento). O MESMO FrameGraph, o MESMO compositor e o MESMO export que
#  rodam sobre o Vulkan rodam sobre este alvo: o que muda é a tradução de
#  `CommandList`/`GPUBackend` para Metal.
#
#  Quem adiciona este diretório é o integrador, com UMA linha em
#  engine/CMakeLists.txt (depois do bloco do aurea_vulkan):
#
#      add_subdirectory(gpu/metal)
#
#  A guarda de plataforma está AQUI dentro: fora de Apple o alvo não é criado e
#  o build segue — por isso a linha pode entrar sem `if`.
#
#  Do lado do idioma: os fontes são Objective-C++ (`.mm`), porque a API é ObjC.
#  ARC (`-fobjc-arc`) é obrigatório: a gerência de memória dos objetos Metal é do
#  compilador, e reter/soltar à mão é onde este backend teria vazamento.
# =============================================================================
if(NOT APPLE AND NOT AUREA_PLATFORM_IOS)
    message(STATUS "Aurea: backend Metal ignorado (nao e Apple)")
    return()
endif()

# O projeto raiz declara C e C++, não ObjC++: sem isto o CMake não sabe compilar
# um `.mm`. Só este alvo usa o idioma.
if(NOT CMAKE_OBJCXX_COMPILER)
    enable_language(OBJCXX)
endif()

set(AUREA_METAL_SOURCES
    MetalCommon.mm      # enum do motor → enum do Metal, anel de memória, instância
    MetalDevice.mm      # dispositivo, capacidades, frames, medição, cache de pipeline
    MetalResources.mm   # texturas, buffers, samplers, shaders, pipelines, upload/leitura
    MetalCommands.mm    # CommandList, render pass, encoders, ciclo do frame
    MetalSurface.mm     # CAMetalLayer, drawable
)

add_library(aurea_metal STATIC ${AUREA_METAL_SOURCES})

target_include_directories(aurea_metal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aurea_metal PUBLIC aurea_core)

# O backend se identifica para quem pergunta (e para um eventual build que queira
# escolher o alvo em tempo de configuração).
target_compile_definitions(aurea_metal PUBLIC AUREA_GPU_METAL=1)

# ARC + os mesmos avisos do resto do motor. `-Wno-...` só onde a API ObjC obriga.
target_compile_options(aurea_metal PRIVATE
    ${AUREA_WARNINGS}
    -fobjc-arc
    # `id<MTLTexture>` como membro de struct C++: o ARC cuida da cópia e da
    # destruição, e o aviso de "move não gerado" não se aplica (não movemos).
    $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wno-objc-missing-super-calls>)

set_target_properties(aurea_metal PROPERTIES
    OBJCXX_STANDARD 23
    OBJCXX_STANDARD_REQUIRED ON
    OBJCXX_EXTENSIONS OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# Frameworks. `Foundation` e `Metal` são o essencial; `QuartzCore` traz a
# CAMetalLayer e `CoreVideo` o CVMetalTextureCache do zero-copy de vídeo.
target_link_libraries(aurea_metal PUBLIC
    "-framework Foundation"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework CoreVideo"
    "-framework CoreGraphics")
