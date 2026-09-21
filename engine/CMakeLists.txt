# =============================================================================
#  AUREA ENGINE — núcleo C++23 compartilhado entre Android e iOS.
#
#  Regra central de performance:
#     CPU organiza. GPU processa. Hardware decodifica. Hardware codifica.
#     UI apenas controla.
#
#  Este projeto não conhece Compose, SwiftUI, Vulkan nem Metal diretamente:
#  são alvos que entram por baixo de interfaces puras (IGPUBackend,
#  IMediaDecoder, IPlatformSurface). Quem compila para Android liga
#  AUREA_PLATFORM_ANDROID; para iOS, AUREA_PLATFORM_IOS.
# =============================================================================
cmake_minimum_required(VERSION 3.22)

project(AureaEngine
        VERSION 2.0.0
        DESCRIPTION "Aurea Editor — motor de composicao e render"
        LANGUAGES CXX)

# -----------------------------------------------------------------------------
# Padrão da linguagem
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# -----------------------------------------------------------------------------
# Plataforma
# -----------------------------------------------------------------------------
if(ANDROID)
    set(AUREA_PLATFORM_ANDROID ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(AUREA_PLATFORM_IOS ON)
else()
    set(AUREA_PLATFORM_HOST ON)
endif()

option(AUREA_BUILD_TESTS      "Compila os testes do motor"      ON)
option(AUREA_BUILD_SHADERS    "Compila os shaders SPIR-V/MSL"   OFF)
option(AUREA_ENABLE_SANITIZE  "ASan/UBSan no host"              OFF)

# -----------------------------------------------------------------------------
# Alvo: aurea_core
#
# Tudo que é independente de GPU e de sistema operacional. É a única parte
# que a UI enxerga. Não faz I/O de vídeo, não fala com o display.
# -----------------------------------------------------------------------------
set(AUREA_CORE_SOURCES
    src/core/Log.cpp
    src/core/Time.cpp
    src/memory/MemoryManager.cpp
    src/jobs/JobSystem.cpp
    src/command/UndoStack.cpp
    src/platform/DeviceCapabilities.cpp
    src/animation/Curve.cpp
    src/animation/Track.cpp
    src/timeline/Composition.cpp
    src/timeline/Timeline.cpp
    src/project/Project.cpp
    src/project/Serialization.cpp
    src/render/GPUBackend.cpp
    src/render/FrameGraph.cpp
    src/render/EffectGraph.cpp
    src/render/ShaderLibrary.cpp
    src/render/RenderScheduler.cpp
    src/engine/Engine.cpp
)

add_library(aurea_core STATIC ${AUREA_CORE_SOURCES})

target_include_directories(aurea_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(aurea_core
    PUBLIC
        AUREA_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        AUREA_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        AUREA_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    PRIVATE
        # O motor usa fopen/fwrite direto em vez de <fstream>: o comportamento
        # de exceção do iostream não combina com um motor compilado sem
        # exceções, e formatar via stream custa mais do que o necessário para
        # dados binários. O aviso de "função insegura" do MSVC não se aplica —
        # todos os caminhos vêm de strings que o próprio motor montou.
        $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)

if(AUREA_PLATFORM_ANDROID)
    target_compile_definitions(aurea_core PUBLIC AUREA_PLATFORM_ANDROID=1)
elseif(AUREA_PLATFORM_IOS)
    target_compile_definitions(aurea_core PUBLIC AUREA_PLATFORM_IOS=1)
else()
    target_compile_definitions(aurea_core PUBLIC AUREA_PLATFORM_HOST=1)
endif()

# O motor é usado por JNI e por ObjC++: RTTI ligado ajuda o bridge a validar
# tipos; exceções DESLIGADAS porque nenhuma fronteira do motor pode lançar.
target_compile_options(aurea_core PRIVATE
    # /utf-8: o código-fonte é UTF-8 e tem acentos e setas nos comentários. Sem
    # esta flag o MSVC lê com a página de código do sistema, e uma sequência de
    # vários bytes no meio de um comentário consome caracteres da linha
    # seguinte — o erro que aparece é "identificador não declarado" num lugar
    # que não tem nada a ver com o problema.
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc- /Zc:__cplusplus /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -fno-exceptions>
)

target_compile_options(aurea_core PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-O3 -ffast-math>
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-O2 -g>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2>
)

if(AUREA_ENABLE_SANITIZE AND NOT ANDROID AND NOT AUREA_PLATFORM_IOS)
    target_compile_options(aurea_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(aurea_core PRIVATE -fsanitize=address,undefined)
endif()

# `pthread` e uma biblioteca separada no Linux e no host do macOS. No Android
# ela NAO existe como biblioteca: o suporte a threads vive na libc, e pedir
# `-lpthread` faz o linker falhar com "unable to find library". O iOS tambem
# nao tem libpthread separada.
if(NOT MSVC AND NOT ANDROID AND NOT AUREA_PLATFORM_IOS)
    find_package(Threads REQUIRED)
    target_link_libraries(aurea_core PUBLIC Threads::Threads)
endif()

# -----------------------------------------------------------------------------
# Nota sobre shaders:
#
#  A fonte dos shaders é única e mora em `include/aurea/shaders/`. Ela é
#  compilada para SPIR-V no build; o iOS recebe MSL gerado de SPIR-V por
#  SPIRV-Cross, não uma versão escrita à mão. É por isso que preview e export
#  são visualmente idênticos mesmo com backends diferentes: só existe uma
#  implementação.
#
#  A etapa de compilação offline (glslang + spirv-cross) entra junto com o
#  backend gráfico — a opção AUREA_BUILD_SHADERS existe e fica desligada
#  enquanto não houver shader de efeito para compilar.
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# Alvo: aurea — a biblioteca compartilhada que a UI nativa carrega.
#
# Existe só no Android e no iOS. No host, quem consome o motor é o executável de
# teste, que linka `aurea_core` direto — sem .so no meio, para o depurador
# enxergar os símbolos.
#
# A ponte NÃO entra em `aurea_core`: o núcleo precisa compilar sem <jni.h> e sem
# cabeçalhos de sistema da Apple, senão deixa de rodar nos testes.
# -----------------------------------------------------------------------------
if(AUREA_PLATFORM_ANDROID)
    add_library(aurea SHARED
        platform/android/aurea_jni.cpp
    )
    target_link_libraries(aurea PRIVATE aurea_core android log)
    target_compile_options(aurea PRIVATE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -fno-exceptions -fno-rtti>
    )
    # O motor é um só, carregado uma vez no processo. O nome do arquivo tem que
    # bater com o `System.loadLibrary("aurea")` do Kotlin.
    set_target_properties(aurea PROPERTIES
        OUTPUT_NAME "aurea"
        PREFIX "lib"
        ANDROID_STL "c++_shared"
    )
endif()

# -----------------------------------------------------------------------------
# Testes
# -----------------------------------------------------------------------------
if(AUREA_BUILD_TESTS AND NOT ANDROID AND NOT AUREA_PLATFORM_IOS)
    enable_testing()
    add_subdirectory(tests)
endif()
