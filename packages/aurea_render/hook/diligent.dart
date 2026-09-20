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
/// backends de sete APIs (D3D11, D3D12, Metal, OpenGL, WebGPU...) e o Aurea
/// usa DOIS — Vulkan no Android e no Windows, Metal no iPhone. Puxar o
/// resto seria compilar o que nunca roda.
///
/// O QUE ESTA AQUI FOI MEDIDO, NAO ADIVINHADO. Em 18/09 a lista parecia
/// pronta e nao compilava: eram quatro muros que so aparecem quando um
/// `clang++` de verdade roda em cima — a ORDEM dos cabecalhos, os macros de
/// API suportada, o `__forceinline` e o namespace do SPIRV-Cross. Cada um
/// esta comentado no lugar onde morde.
class Fontes3D {
  const Fontes3D._();

  /// A RAIZ DAS DEPENDENCIAS, ao lado deste pacote.
  static const raiz = 'third_party';

  /// ARQUIVO QUE NAO ENTRA MESMO ESTANDO NA PASTA.
  ///
  /// - `DLLMain.cpp` e do carregador do Windows (`Windows.h`).
  /// - Os outros pedem tres coisas que o vendor NAO trouxe: o glslang (o
  ///   submodulo veio vazio), o DXC e o tint/WGSL. Eles so servem para
  ///   COMPILAR shader em tempo de execucao, e o Aurea entrega SPIR-V
  ///   pronto — os macros `DILIGENT_NO_GLSLANG` / `DILIGENT_NO_HLSL` em
  ///   [defines] desligam os caminhos que chamariam esses arquivos.
  /// - `ZipArchiveIOSystem.cpp` arrasta o minizip + o zlib do contrib, e
  ///   quem instancia a classe sao os importadores 3MF e Collada, os dois
  ///   desligados. O zlib que sobra e o do sistema ([bibliotecas]).
  static const _fora = {
    'DLLMain.cpp',
    'GLSLangUtils.cpp',
    'DXBCUtils.cpp',
    'DXCompiler.cpp',
    'DXCompilerLibrary.cpp',
    'DXCompilerLibraryLinux.cpp',
    'DXCompilerLibraryUWP.cpp',
    'DXCompilerLibraryWin32.cpp',
    'HLSLUtils.cpp',
    'SPIRVTools.cpp',
    'WGSLUtils.cpp',
    'WGSLShaderResources.cpp',
    'ZipArchiveIOSystem.cpp',
    'Exporter.cpp',
    'glTFExporter.cpp',
    'glTF2Exporter.cpp',
    'FBXExporter.cpp',
    'FBXExportNode.cpp',
    'FBXExportProperty.cpp',
    'ObjExporter.cpp',
  };

  /// A PASTA DO `ShaderTools` TEM OITO ARQUIVOS QUE COMPILAM E SEIS QUE
  /// NAO. Varrer a pasta inteira derrubaria o build, entao aqui vao os oito
  /// pelo nome — e sao poucos justamente porque o SPIRV-Cross faz o
  /// trabalho pesado.
  static const _shaderTools = [
    'Graphics/ShaderTools/src/SPIRVUtils.cpp',
    'Graphics/ShaderTools/src/SPIRVShaderResources.cpp',
    'Graphics/ShaderTools/src/ShaderToolsCommon.cpp',
    'Graphics/ShaderTools/src/GLSLParsingTools.cpp',
    'Graphics/ShaderTools/src/HLSLParsingTools.cpp',
    'Graphics/ShaderTools/src/HLSLTokenizer.cpp',
    'Graphics/ShaderTools/src/DXILUtilsStub.cpp',
    'Graphics/ShaderTools/src/GLSLUtils.cpp',
  ];

  /// O SPIRV-CROSS MORA NA RAIZ DO PACOTE DE TERCEIRO, e nao numa pasta
  /// `spirv_cross/`. Sao cinco arquivos e eles entram sempre.
  static const _spirvCross = [
    'ThirdParty/SPIRV-Cross/spirv_cross.cpp',
    'ThirdParty/SPIRV-Cross/spirv_cross_parsed_ir.cpp',
    'ThirdParty/SPIRV-Cross/spirv_cfg.cpp',
    'ThirdParty/SPIRV-Cross/spirv_parser.cpp',
    'ThirdParty/SPIRV-Cross/spirv_cross_util.cpp',
  ];

  static const _pastasDiligent = [
    'diligent/Common/src',
    'diligent/Graphics/GraphicsEngine/src',
    'diligent/Graphics/GraphicsEngineVulkan/src',
    'diligent/Graphics/GraphicsAccessories/src',
  ];

  /// O ASSIMP TEM UMA PASTA POR FORMATO. O Aurea abre GLB/GLTF, FBX e
  /// OBJ — e o resto do importador custa tempo de build e tamanho de
  /// binario sem servir a ninguem. As pastas de baixo sao as que os tres
  /// formatos precisam para abrir de verdade: material, textura, malha,
  /// esqueleto e as utilidades comuns.
  ///
  /// A PASTA DO OBJ SE CHAMA `Obj`, e nao `OBJ`: o disco decide. Os
  /// importadores desligados em [defines] nao entram nem na lista nem no
  /// binario.
  static const _pastasAssimp = [
    'assimp/code/Common',
    'assimp/code/Material',
    'assimp/code/AssetLib/glTF',
    'assimp/code/AssetLib/glTF2',
    'assimp/code/AssetLib/FBX',
    'assimp/code/AssetLib/Obj',
    'assimp/code/Geometry',
    'assimp/code/PostProcessing',
  ];

  static List<String> _cpp(String pasta, {bool recursivo = true}) {
    final dir = Directory('$raiz/$pasta');
    if (!dir.existsSync()) return const [];
    return [
      for (final f in dir.listSync(recursive: recursivo))
        if (f is File &&
            f.path.endsWith('.cpp') &&
            !_fora.contains(f.uri.pathSegments.last))
          f.path.replaceAll(r'\', '/'),
    ]..sort();
  }

  static List<String> diligent({required bool android, required bool ios}) => [
    for (final p in _pastasDiligent) ..._cpp(p),
    // `EngineFactoryBase` registra o callback global de diagnostico, cuja
    // implementacao mora em `Primitives/src`, fora das quatro arvores de
    // engine acima. O linker aceita deixar o simbolo pendente no `.so`,
    // mas o Android recusa carregar a biblioteca inteira; com isso ate
    // particulas (que nao usam Diligent) ficam indisponiveis.
    if (File('$raiz/diligent/Primitives/src/DebugOutput.cpp').existsSync())
      '$raiz/diligent/Primitives/src/DebugOutput.cpp',
    // O target de plataforma e uma dependencia real do backend (log,
    // arquivos e utilidades). No iOS ha dois .mm; o hook escolhe
    // Objective-C++ para que eles compilem junto com o restante.
    for (final f in const [
      'Platforms/Basic/src/BasicFileSystem.cpp',
      'Platforms/Basic/src/BasicPlatformDebug.cpp',
      'Platforms/Basic/src/BasicPlatformMisc.cpp',
    ])
      if (File('$raiz/diligent/$f').existsSync()) '$raiz/diligent/$f',
    if (ios &&
        File('$raiz/diligent/Platforms/Basic/src/StandardFile.cpp')
            .existsSync())
      '$raiz/diligent/Platforms/Basic/src/StandardFile.cpp',
    if (android)
      for (final f in const [
        'Platforms/Android/src/AndroidDebug.cpp',
        'Platforms/Android/src/AndroidFileSystem.cpp',
        'Platforms/Android/src/AndroidPlatformMisc.cpp',
        'Platforms/Linux/src/LinuxFileSystem.cpp',
      ])
        if (File('$raiz/diligent/$f').existsSync()) '$raiz/diligent/$f',
    if (ios)
      for (final f in const [
        'Platforms/Apple/src/AppleDebug.mm',
        'Platforms/Apple/src/AppleFileSystem.mm',
        'Platforms/Apple/src/ApplePlatformMisc.cpp',
        'Platforms/Linux/src/LinuxFileSystem.cpp',
      ])
        if (File('$raiz/diligent/$f').existsSync()) '$raiz/diligent/$f',
    for (final f in _shaderTools)
      if (File('$raiz/diligent/$f').existsSync()) '$raiz/diligent/$f',
    for (final f in _spirvCross)
      if (File('$raiz/diligent/$f').existsSync()) '$raiz/diligent/$f',
  ];

  static List<String> assimp() => [
    for (final p in _pastasAssimp) ..._cpp(p, recursivo: false),
    // `Common/Assimp.cpp` instancia este adaptador da API C. Sem a
    // implementacao, o Android aceita gerar o .so com o vtable pendente,
    // mas o carregador recusa a biblioteca inteira em tempo de execucao —
    // inclusive os simbolos de particulas que nao usam Assimp.
    if (File('$raiz/assimp/code/CApi/CInterfaceIOWrapper.cpp').existsSync())
      '$raiz/assimp/code/CApi/CInterfaceIOWrapper.cpp',
  ];

  /// OS CABECALHOS. Sem eles o Diligent nem sabe que a Vulkan existe.
  ///
  /// A ORDEM E O PRIMEIRO MURO, e nao e questao de gosto: CADA ALVO DO
  /// DILIGENT TEM O SEU PROPRIO `pch.h`, e o `Common/include` tambem tem um.
  /// Com o `Common` na frente, todo arquivo do backend Vulkan pega o
  /// pre-compilado ERRADO — que nao inclui os cabecalhos da Vulkan — e o
  /// erro que sai na tela e `unknown type name 'VkDevice'` em quarenta
  /// arquivos, apontando para o lugar errado. O backend primeiro, o
  /// `Common` por ultimo.
  static List<String> includes({required bool android, required bool apple}) {
    const raizes = [
      'Graphics/GraphicsEngineVulkan',
      'Graphics/GraphicsEngine',
      'Graphics/GraphicsEngineNextGenBase',
      'Graphics/GraphicsAccessories',
      'Graphics/ShaderTools',
      'Graphics/GraphicsTools',
      'Common',
      'Platforms',
    ];
    final dirs = <String>[];
    for (final r in raizes) {
      for (final sub in ['interface', 'include']) {
        if (Directory('$raiz/diligent/$r/$sub').existsSync()) {
          dirs.add('$raiz/diligent/$r/$sub');
        }
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
      if (apple) '$raiz/diligent/Platforms/Linux/interface',
      if (!android && !apple) '$raiz/diligent/Platforms/Win32/interface',
      '$raiz/diligent/ThirdParty/Vulkan-Headers/include',
      '$raiz/diligent/ThirdParty/xxHash',
      // O SPIRV-Cross e incluido por `"spirv_cross.hpp"`, sem pasta.
      '$raiz/diligent/ThirdParty/SPIRV-Cross',
    ]);
    // O ASSIMP VENDORIZA O QUE OS TRES FORMATOS PRECISAM. O `contrib` puro
    // entra porque o `StbCommon.h` escreve `stb/stb_image.h` com a pasta na
    // frente; o `zlib` entra pelos CABECALHOS apenas — quem liga e o zlib
    // do sistema, porque o do contrib e de 1999 e so compila como C.
    for (final c in [
      'contrib/rapidjson/include',
      'contrib',
      'contrib/utf8cpp/source',
      'contrib/zlib',
    ]) {
      if (Directory('$raiz/assimp/$c').existsSync()) {
        dirs.add('$raiz/assimp/$c');
      }
    }
    dirs.addAll(['$raiz/assimp/include', '$raiz/assimp/code']);
    return dirs;
  }

  /// O SEGUNDO E O TERCEIRO MURO ESTAO AQUI.
  ///
  /// O DILIGENT NAO ESCOLHE A API SOZINHO: `Defines.h` exige que alguem
  /// diga qual esta suportada, e sem isso o build para em "No API is
  /// supported on this platform". Sao QUINZE macros, e os `=0` importam
  /// tanto quanto os `=1`.
  ///
  /// O `__forceinline` e do `CMakeLists.txt` do proprio Diligent
  /// (linha 448): no MSVC ele existe, no clang nao, e o Diligent escreve
  /// assim em quarenta arquivos.
  ///
  /// O `SPIRV_CROSS_NAMESPACE_OVERRIDE` e do `ThirdParty/CMakeLists.txt`
  /// (linha 111) e o `DILIGENT_SPIRV_CROSS_NAMESPACE` e do ShaderTools. Sem
  /// os dois, `SPIRVUtils.cpp` nao acha o `diligent_spirv_cross`.
  ///
  /// O ASSIMP: so GLB/GLTF, FBX e OBJ. Cada importador extra arrasta um
  /// contrib proprio (pugixml, openddlparser, draco, tinyusdz) e o vendor
  /// nao trouxe metade deles — o `tinyusdz` e literalmente uma pasta com um
  /// patch dentro. Sem `ASSIMP_BUILD_NO_EXPORT` os exporters de glTF puxam
  /// o mesmo rapidjson por outro caminho, e exportar modelo nao e caso de
  /// uso do app.
  static Map<String, String?> defines({
    required bool android,
    required bool ios,
  }) => {
    if (android) 'ANDROID': null,
    if (android) 'PLATFORM_ANDROID': '1',
    if (ios) 'PLATFORM_IOS': '1',
    if (ios) 'PLATFORM_APPLE': '1',
    if (android) 'VK_USE_PLATFORM_ANDROID_KHR': '1',
    if (ios) 'VK_USE_PLATFORM_METAL_EXT': '1',
    // A tag 2.5.6 publicou um unico uso de `std::ios_base::open_mode` no
    // arquivo Android; o nome padrao da libc++ e `openmode`. O CMake
    // oficial aplica a compatibilidade no target, e o build hook precisa
    // declará-la explicitamente.
    if (android) 'open_mode': 'openmode',
    '__forceinline': 'inline',
    'VULKAN_SUPPORTED': '1',
    'D3D11_SUPPORTED': '0',
    'D3D12_SUPPORTED': '0',
    'GL_SUPPORTED': '0',
    'GLES_SUPPORTED': '0',
    'METAL_SUPPORTED': '0',
    'WEBGPU_SUPPORTED': '0',
    'DILIGENT_NO_GLSLANG': '1',
    'DILIGENT_NO_HLSL': '1',
    'DILIGENT_NO_DIRECT3D11': '1',
    'DILIGENT_NO_DIRECT3D12': '1',
    'DILIGENT_NO_OPENGL': '1',
    'DILIGENT_NO_WEBGPU': '1',
    'SPIRV_CROSS_NAMESPACE_OVERRIDE': 'diligent_spirv_cross',
    'DILIGENT_SPIRV_CROSS_NAMESPACE': 'diligent_spirv_cross',
    for (final m in _assimpDesligados) m: '1',
  };

  static const _assimpDesligados = [
    'ASSIMP_BUILD_NO_EXPORT',
    'ASSIMP_BUILD_NO_3DS_IMPORTER',
    'ASSIMP_BUILD_NO_3D_IMPORTER',
    'ASSIMP_BUILD_NO_3MF_IMPORTER',
    'ASSIMP_BUILD_NO_AC_IMPORTER',
    'ASSIMP_BUILD_NO_AMF_IMPORTER',
    'ASSIMP_BUILD_NO_ASE_IMPORTER',
    'ASSIMP_BUILD_NO_ASSBIN_IMPORTER',
    'ASSIMP_BUILD_NO_ASSJSON_IMPORTER',
    'ASSIMP_BUILD_NO_ASSXML_IMPORTER',
    'ASSIMP_BUILD_NO_B3D_IMPORTER',
    'ASSIMP_BUILD_NO_BLEND_IMPORTER',
    'ASSIMP_BUILD_NO_BVH_IMPORTER',
    'ASSIMP_BUILD_NO_C4D_IMPORTER',
    'ASSIMP_BUILD_NO_COB_IMPORTER',
    'ASSIMP_BUILD_NO_COLLADA_IMPORTER',
    'ASSIMP_BUILD_NO_CSM_IMPORTER',
    'ASSIMP_BUILD_NO_DXF_IMPORTER',
    'ASSIMP_BUILD_NO_HMP_IMPORTER',
    'ASSIMP_BUILD_NO_IFC_IMPORTER',
    'ASSIMP_BUILD_NO_IQM_IMPORTER',
    'ASSIMP_BUILD_NO_IRRMESH_IMPORTER',
    'ASSIMP_BUILD_NO_IRR_IMPORTER',
    'ASSIMP_BUILD_NO_LWO_IMPORTER',
    'ASSIMP_BUILD_NO_LWS_IMPORTER',
    'ASSIMP_BUILD_NO_M3D_IMPORTER',
    'ASSIMP_BUILD_NO_MD2_IMPORTER',
    'ASSIMP_BUILD_NO_MD3_IMPORTER',
    'ASSIMP_BUILD_NO_MD5_IMPORTER',
    'ASSIMP_BUILD_NO_MDC_IMPORTER',
    'ASSIMP_BUILD_NO_MDL_IMPORTER',
    'ASSIMP_BUILD_NO_MMD_IMPORTER',
    'ASSIMP_BUILD_NO_MS3D_IMPORTER',
    'ASSIMP_BUILD_NO_NDO_IMPORTER',
    'ASSIMP_BUILD_NO_NFF_IMPORTER',
    'ASSIMP_BUILD_NO_OFF_IMPORTER',
    'ASSIMP_BUILD_NO_OGRE_IMPORTER',
    'ASSIMP_BUILD_NO_OPENGEX_IMPORTER',
    'ASSIMP_BUILD_NO_PLY_IMPORTER',
    'ASSIMP_BUILD_NO_Q3BSP_IMPORTER',
    'ASSIMP_BUILD_NO_Q3D_IMPORTER',
    'ASSIMP_BUILD_NO_RAW_IMPORTER',
    'ASSIMP_BUILD_NO_SIB_IMPORTER',
    'ASSIMP_BUILD_NO_SMD_IMPORTER',
    'ASSIMP_BUILD_NO_STL_IMPORTER',
    'ASSIMP_BUILD_NO_TERRAGEN_IMPORTER',
    'ASSIMP_BUILD_NO_USD_IMPORTER',
    'ASSIMP_BUILD_NO_X3D_IMPORTER',
    'ASSIMP_BUILD_NO_XGL_IMPORTER',
    'ASSIMP_BUILD_NO_X_IMPORTER',
    'ASSIMP_BUILD_NO_GLTF2_EXPORTER',
    'ASSIMP_BUILD_NO_GLTF_EXPORTER',
  ];

  /// AS BIBLIOTECAS QUE O DILIGENT PRECISA TER AO LADO.
  ///
  /// O `z` e do SISTEMA nas duas pontas (o bionic do Android e a libSystem
  /// do iPhone), e nao o do contrib: aquele e de 1999, tem funcao em estilo
  /// K&R, e nao compila como C++. O `Compression.cpp` do Assimp — que o
  /// FBX usa — so precisa dos cabecalhos e chama o zlib de verdade na
  /// ligacao.
  static List<String> bibliotecas({
    required bool android,
    required bool apple,
  }) => [
    if (android) ...['vulkan', 'log', 'android'],
    if (android || apple) 'z',
  ];
}
