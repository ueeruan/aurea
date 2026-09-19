import 'dart:io';

/// AS FONTES DO DILIGENT E DO ASSIMP, LISTADAS EM TEMPO DE BUILD.
///
/// ELAS SAO MUITAS (o Diligent sozinho passa de 150 `.cpp`) e mudam de
/// versao para versao. Uma lista escrita a mao no hook envelhece no dia
/// seguinte: sobra arquivo que nao existe mais e falta o que entrou, e o
/// sintoma e um erro de link que ninguem liga a uma atualizacao de
/// dependencia. Aqui a lista e LIDA do disco, e o que existe e o que
/// compila.
///
/// AS PASTAS SAO ESCOLHIDAS A DEDO, e isso e de proposito: o Diligent traz
/// backends de sete APIs (D3D11, D3D12, Metal, OpenGL, WebGPU...) e o
/// Aurea usa DOIS — Vulkan no Android e no Windows, Metal no iPhone. Puxar
/// o resto seria compilar o que nunca roda.
class Fontes3D {
  const Fontes3D._();

  /// A RAIZ DAS DEPENDENCIAS, ao lado deste pacote.
  static const raiz = 'third_party';

  /// SO O QUE A CASA USA. A ordem nao importa: o compilador resolve.
  static const _pastasDiligent = [
    'diligent/Common/src',
    'diligent/Graphics/GraphicsEngine/src',
    'diligent/Graphics/GraphicsEngineVulkan/src',
    'diligent/Graphics/GraphicsAccessories/src',
    'diligent/Graphics/ShaderTools/src',
  ];

  /// O ASSIMP TEM UMA PASTA POR FORMATO. O Aurea abre GLB/GLTF, FBX e
  /// OBJ — e o resto do importador custa tempo de build e tamanho de
  /// binario sem servir a ninguem. As pastas de baixo sao as que os tres
  /// formatos precisam para abrir de verdade: material, textura, malha,
  /// esqueleto e as utilidades comuns.
  static const _pastasAssimp = [
    'assimp/code/Common',
    'assimp/code/Material',
    'assimp/code/AssetLib/glTF',
    'assimp/code/AssetLib/glTF2',
    'assimp/code/AssetLib/FBX',
    'assimp/code/AssetLib/OBJ',
    'assimp/code/AssetLib/STEP',
    'assimp/code/Geometry',
    'assimp/code/PostProcessing',
    'assimp/code/PBR',
    'assimp/code/Res',
  ];

  static List<String> _cpp(String pasta) {
    final dir = Directory('$raiz/$pasta');
    if (!dir.existsSync()) return const [];
    return [
      for (final f in dir.listSync(recursive: true))
        if (f is File && f.path.endsWith('.cpp'))
          f.path.replaceAll(r'\', '/'),
    ]..sort();
  }

  static List<String> diligent() =>
      [for (final p in _pastasDiligent) ..._cpp(p)];

  static List<String> assimp() => [for (final p in _pastasAssimp) ..._cpp(p)];

  /// OS CABECALHOS. Sem eles o Diligent nem sabe que a Vulkan existe.
  static List<String> includes({required bool android, required bool apple}) {
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
    // O DILIGENT SEPARA POR PLATAFORMA: `Primitives` e `Platforms/Basic`
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
    return dirs;
  }
}
