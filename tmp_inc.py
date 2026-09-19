import io, sys, os
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

# 1) o include do Diligent e `include/` + `interface/`, e nao `src/`
p = 'packages/aurea_render/hook/diligent.dart'
s = open(p, encoding='utf-8').read()
ini = s.index('  static List<String> includes(')
fim = s.index('      ];', ini) + len('      ];')
novo = """  static List<String> includes({required bool android, required bool apple}) {
    // O DILIGENT GUARDA OS CABECALHOS EM `include/` E AS INTERFACES
    // PUBLICAS EM `interface/`. As duas entram: a segunda e o que os
    // fontes de fora (o nosso `api_3d.cpp`) usam, e a primeira e o que os
    // proprios fontes dele usam internamente.
    const raizes = [
      'diligent/Common',
      'diligent/Platforms',
      'diligent/Graphics/GraphicsEngine',
      'diligent/Graphics/GraphicsEngineVulkan',
      'diligent/Graphics/GraphicsEngineNextGenBase',
      'diligent/Graphics/GraphicsAccessories',
      'diligent/Graphics/ShaderTools',
      'diligent/Graphics/GraphicsTools',
    ];
    final dirs = <String>[];
    for (final r in raizes) {
      for (final sub in ['interface', 'include']) {
        if (Directory('$raiz/$r/$sub').existsSync()) dirs.add('$raiz/$r/$sub');
      }
    }
    dirs.addAll([
      '$raiz/diligent/ThirdParty/Vulkan-Headers/include',
      '$raiz/diligent/ThirdParty/xxHash',
      '$raiz/assimp/include',
      '$raiz/assimp/code',
    ]);
    return dirs;
  }"""
s = s[:ini] + novo + s[fim:]
open(p, 'w', encoding='utf-8').write(s)
print('includes corrigidos')

# 2) o `preparar` do .cpp devolve int, como o .h diz
p = 'packages/aurea_render/src/api_3d.cpp'
s = open(p, encoding='utf-8').read()
s = s.replace('Resulta<int> preparar() {', 'int preparar() {')
s = s.replace('#include "base.h"\n', '')
open(p, 'w', encoding='utf-8').write(s)
print('preparar devolve int')
