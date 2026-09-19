import io, sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

p = 'packages/aurea_render/hook/build.dart'
s = open(p, encoding='utf-8').read()

s = s.replace("""import 'package:code_assets/code_assets.dart';""",
              """import 'diligent.dart';

import 'package:code_assets/code_assets.dart';""", 1)

s = s.replace("""    final android = input.config.code.targetOS == OS.android;""",
              """    final android = input.config.code.targetOS == OS.android;
    final apple = input.config.code.targetOS == OS.iOS ||
        input.config.code.targetOS == OS.macOS;
    // O 3D ENTRA NO MESMO RENDERCORE, e nao num pacote a parte: a cena
    // montada pela timeline tem de compor com o 2D NO MESMO QUADRO, e
    // dois renderizadores separados so se compoem por textura no meio.
    final fontes3d = [...Fontes3D.diligent(), ...Fontes3D.assimp()];
    final includes3d = Fontes3D.includes(android: android, apple: apple);""", 1)

s = s.replace("""      includes: const ['src'],""",
              """      includes: ['src', ...includes3d],""", 1)

s = s.replace("""      std: 'c++20',
      flags: [if (input.config.code.targetOS == OS.windows) '/EHsc'],""",
              """      std: 'c++20',
      flags: [
        if (input.config.code.targetOS == OS.windows) '/EHsc',
        // O BACKEND E O VULKAN. Sem esta lista o Diligent tenta compilar
        // D3D11, D3D12, OpenGL e WebGPU junto — quatro APIs que o Aurea
        // nunca abre, cada uma com as suas dependencias de sistema.
        '-DDILIGENT_NO_D3D11',
        '-DDILIGENT_NO_D3D12',
        '-DDILIGENT_NO_OPENGL',
        '-DDILIGENT_NO_GLSLANG',
        // SEM HLSL: os shaders da casa sao escritos em GLSL e viram SPIR-V
        // no build (o `glslc` vem no proprio NDK). Com HLSL ligado, o
        // Diligent puxaria o SPIRV-Tools inteiro para traduzir uma
        // linguagem que ninguem aqui escreve.
        '-DDILIGENT_NO_HLSL',
        '-DDILIGENT_DISABLE_INTERNAL_SHADER_COMPILATION',
        if (android) ...['-DVK_USE_PLATFORM_ANDROID_KHR', '-DPLATFORM_ANDROID'],
        if (apple) ...['-DVK_USE_PLATFORM_METAL_EXT', '-DPLATFORM_APPLE'],
        if (input.config.code.targetOS == OS.windows)
          ...['-DVK_USE_PLATFORM_WIN32_KHR', '-DPLATFORM_WIN32'],
        // O ASSIMP DIZ O QUE ABRIU. Sem isto ele compila todos os
        // importadores que a pasta tiver, inclusive os que so servem a
        // formatos que a casa nao abre.
        '-DASSIMP_BUILD_NO_EXPORT',
        '-DASSIMP_BUILD_NO_C4D_IMPORTER',
        '-DASSIMP_BUILD_NO_X_IMPORTER',
        '-DASSIMP_BUILD_NO_3DS_IMPORTER',
        '-DASSIMP_BUILD_NO_MD2_IMPORTER',
        '-DASSIMP_BUILD_NO_MD3_IMPORTER',
        '-DASSIMP_BUILD_NO_MD5_IMPORTER',
        '-DASSIMP_BUILD_NO_MDL_IMPORTER',
        '-DASSIMP_BUILD_NO_3MF_IMPORTER',
      ],""", 1)

s = s.replace("""        'src/nucleo.cpp',
        'src/api.cpp',
      ],""",
              """        'src/nucleo.cpp',
        'src/api.cpp',
        'src/api_3d.cpp',
        ...fontes3d,
      ],""", 1)

open(p, 'w', encoding='utf-8').write(s)
print('hook preparado; fontes3d entram na lista')
