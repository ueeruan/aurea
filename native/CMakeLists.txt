cmake_minimum_required(VERSION 3.22.1)
project(aurea_native VERSION 1.0.0 LANGUAGES C CXX)

# AQUI SO VIVE O QUE ESTA LIGADO.
#
# Este arquivo construia `libaurea_native.so` a partir de 25.185 linhas de
# C++ (core, animacao, renderer, rendercore, efeitos, scene3d, video,
# optical_flow, preview, export e bridge). A auditoria de 17/09 mostrou tres
# coisas, e as tres juntas condenaram o modulo:
#
#   1. NINGUEM ABRIA A BIBLIOTECA. O unico `DynamicLibrary.open` estava
#      dentro de `lib/src/features/native/`, que por sua vez so era
#      importado por um teste — e esse teste AFIRMAVA que ela nao carrega.
#      O .so ia para dentro do APK em tres ABIs para nunca ser carregado.
#
#   2. ELA NAO COMPILAVA NO iOS. O `project.pbxproj` nunca teve uma fase de
#      CMake; o ramo `elseif(APPLE)` abaixo linkava Metal e AVFoundation em
#      codigo que nunca rodou, dando a impressao falsa de suporte.
#
#   3. LIGAR ELA HOJE DERRUBA O APP. `RenderCore` renderiza com um
#      `RenderEngine` criado no construtor e nunca inicializado
#      (rendercore/RenderCore.cpp), entao `renderer2D_` e nulo e e
#      desreferenciado no laco de camadas. E SIGSEGV na thread de trabalho:
#      o Dart nao tem como capturar.
#
# O que ficou e o que esta VIVO: `libaurea_enhance.so` — Real-ESRGAN e RIFE
# sobre ncnn/Vulkan, usado pela tela Aprimorar e pela exportacao
# (`native_enhancer.dart`, `native_interpolator.dart`). Ele sempre foi um
# alvo separado, com CMake proprio.
#
# Se o motor nativo voltar um dia, o ponto de partida e o historico do git,
# e nao este arquivo: ele volta para ser ligado DEPOIS de o RenderCore ser
# inicializado de verdade, nao antes.

if (NOT ANDROID)
    # Somente Android tem o ncnn/Vulkan empacotado por este caminho.
    return()
endif()

add_subdirectory(enhance)
