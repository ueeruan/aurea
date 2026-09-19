import io, sys, os, glob
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

p = 'packages/aurea_render/hook/diligent.dart'
s = open(p, encoding='utf-8').read()
ini = s.index('    dirs.addAll([')
fim = s.index('    return dirs;')
novo = """    // O DILIGENT SEPARA POR PLATAFORMA: `Primitives` e `Platforms/Basic`
    // valem sempre, e o resto e o que a API de desenho pede (Win32 no
    // Windows, Android no celular, Apple no iPhone).
    dirs.addAll([
      '$raiz/diligent/Primitives/interface',
      '$raiz/diligent/Platforms/Basic/interface',
      if (android) '$raiz/diligent/Platforms/Android/interface',
      if (apple) '$raiz/diligent/Platforms/Apple/interface',
      if (!android && !apple) '$raiz/diligent/Platforms/Win32/interface',
      '$raiz/diligent/ThirdParty/Vulkan-Headers/include',
      '$raiz/diligent/ThirdParty/xxHash',
    ]);
    // O ASSIMP VENDORIZA O QUE PRECISA (rapidjson, zlib, stb, utf8) em
    // `contrib/` — cada um com a sua propria raiz de cabecalho.
    for (final c in [
      'rapidjson/include',
      'zlib',
      'stb',
      'utf8cpp/source',
      'unzip',
      'zip/src',
      'openddlparser/include',
      'pugixml/src',
      'poly2tri',
      'clipper',
      'draco/src',
      'draco/src/draco',
      'Open3DGC',
      'tinyusdz/src',
      'tinyusdz/src/external',
    ]) {
      if (Directory('$raiz/assimp/contrib/$c').existsSync()) {
        dirs.add('$raiz/assimp/contrib/$c');
      }
    }
    dirs.addAll(['$raiz/assimp/include', '$raiz/assimp/code']);
"""
s = s[:ini] + novo + s[fim:]
open(p, 'w', encoding='utf-8').write(s)
print('includes do assimp e do diligent postos')

# o cabeçalho de revisao, que o CMake do Assimp geraria
alvo = 'packages/aurea_render/third_party/assimp/include/assimp/revision.h'
if not os.path.exists(alvo):
    open(alvo, 'w', encoding='utf-8').write(
        '/* Gerado a mao: o CMake do Assimp escreve este arquivo, e aqui o\n'
        '   build nao passa por CMake. Sem ele o Version.cpp nao compila. */\n'
        '#pragma once\n'
        '#define ASSIMP_VERSION_MAJOR 5\n'
        '#define ASSIMP_VERSION_MINOR 4\n'
        '#define ASSIMP_VERSION_PATCH 3\n'
        '#define ASSIMP_GIT_COMMIT_HASH "aurea-vendored"\n')
    print('revision.h gerado')
