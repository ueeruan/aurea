# =============================================================================
#  Aurea / platform / ios / ios_toolchain.cmake
#
#  Toolchain do target iOS. Uso (num runner macOS com Xcode):
#
#     cmake -S engine/platform/ios -B engine/platform/ios/build/iphoneos \
#           -DCMAKE_TOOLCHAIN_FILE=engine/platform/ios/ios_toolchain.cmake \
#           -DCMAKE_BUILD_TYPE=RelWithDebInfo
#     cmake --build engine/platform/ios/build/iphoneos
#
#  Simulador (Apple Silicon, arm64) ou Intel (x86_64):
#
#     -DAUREA_IOS_SIMULATOR=ON            (sysroot iphonesimulator)
#
#  Este arquivo NÃO compila nada no Windows: o Xcode e o SDK de iOS só existem
#  no macOS. Ele é o contrato que o runner macOS executa.
# =============================================================================

set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_VERSION 1)

# O CMake detecta o compilador do Xcode sozinho quando o sistema é iOS; nada de
# apontar clang na mão (o do Xcode é o único que traz o SDK).
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

option(AUREA_IOS_SIMULATOR "Compila para o simulador (iphonesimulator)" OFF)

if(AUREA_IOS_SIMULATOR)
    set(CMAKE_OSX_SYSROOT "iphonesimulator" CACHE STRING "SDK do iOS (simulador)")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        # Num Mac Intel o x86_64 é o simulador nativo; num Apple Silicon é o
        # arm64. As duas fatias cabem no mesmo .a, então o padrão é o universal
        # do simulador — menos uma variável para o integrador acertar.
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Arquiteturas do simulador")
    endif()
else()
    set(CMAKE_OSX_SYSROOT "iphoneos" CACHE STRING "SDK do iOS (aparelho)")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Arquiteturas do aparelho")
    endif()
endif()

# Piso do app. 16.0 e não 14.0: as folhas contextuais do editor (o painel de
# efeitos, transform, 3D e export) usam `presentationDetents`, que é iOS 16, e
# a navegação da Home usa `NavigationStack`, também 16. O backend Metal sozinho
# caberia em 14 (MTLBinaryArchive); a UI não. Documentado no README.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0" CACHE STRING "Piso de iOS do Aurea")
endif()

# Só bibliotecas do SDK: nada do host vaza para dentro do app.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
