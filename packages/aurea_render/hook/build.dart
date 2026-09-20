import 'dart:io';

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

import 'diligent.dart';

/// O RENDERCORE E COMPILADO PELO BUILD HOOK DO DART.
///
/// E o unico caminho que funciona nas tres pontas do Aurea (iOS no CI,
/// Android release e os testes no Windows) — ver docs/nucleo-cpp.md. Os
/// fontes sao os mesmos para todas as plataformas.
///
/// AS EXCECOES FICAM LIGADAS E SAO PEGAS NA PORTA. Desligar excecoes
/// (`-fno-exceptions`) tornaria a biblioteca abortiva em qualquer
/// alocacao falha, e o comportamento mudaria entre MSVC e clang — o
/// penhor seria pior do que o problema. Quem garante que nada atravessa o
/// FFI e `api.cpp`: todo simbolo `extern "C"` fecha o corpo num
/// `catch (...)`. Ver a regra no topo daquele arquivo.
void main(List<String> args) async {
  await build(args, (input, output) async {
    // Ha uma passagem de hooks sem code assets durante o ciclo de debug.
    // Nela `config.code` nao existe; sair aqui mantem hot reload/run estavel.
    if (!input.config.buildCodeAssets) return;

    final android = input.config.code.targetOS == OS.android;
    final ios = input.config.code.targetOS == OS.iOS;
    final apple = ios || input.config.code.targetOS == OS.macOS;
    // O 3D ENTRA SO NO CELULAR. O Diligent precisa de uma API de desenho
    // que exista na maquina que COMPILA: no Android a Vulkan esta no NDK,
    // no iPhone a Metal e do sistema. No Windows, onde rodam os testes, a
    // Vulkan so existe se o SDK estiver instalado — e amarrar `flutter
    // test` a isso trocaria um teste verde por um build que depende da
    // maquina de quem clona. O que o desktop perde e o 3D; o nucleo 2D
    // continua igual nas tres pontas.
    // O backend Vulkan do Diligent roda sobre o driver Vulkan do Android e
    // sobre o MoltenVK (Metal) no iPhone. O pacote aberto do Diligent nao
    // contem a implementacao Metal; tentar marcar METAL_SUPPORTED aqui
    // compila cabecalhos sem motor nenhum por tras. Manter Vulkan nas duas
    // pontas tambem preserva exatamente os mesmos shaders SPIR-V/PBR.
    final tresD = android || ios;

    const moltenVk =
        'third_party/moltenvk/MoltenVK/static/MoltenVK.xcframework/ios-arm64';
    if (ios && !File('$moltenVk/libMoltenVK.a').existsSync()) {
      throw StateError(
        'MoltenVK do iOS ausente. Rode '
        '`bash packages/aurea_render/tool/preparar_3d.sh --ios` antes do build.',
      );
    }
    await CBuilder.library(
      name: 'aurea_render',
      assetName: 'aurea_render.dart',
      sources: [
        ...const [
          'src/compositor.cpp',
          'src/particulas.cpp',
          'src/api_particulas.cpp',
          'src/gerenciador_de_recursos.cpp',
          'src/gerenciador_de_shaders.cpp',
          'src/avaliador_da_timeline.cpp',
          'src/backend_vulkan.cpp',
          'src/vulkan_superficie.cpp',
          // O `jni_android.cpp` se protege sozinho com `#if __ANDROID__`:
          // fora do Android ele vira um arquivo vazio. Listar sempre e mais
          // simples do que um ramo no hook que so serve para nao compilar
          // nada.
          'src/jni_android.cpp',
          'src/relogio_do_quadro.cpp',
          'src/nucleo.cpp',
          'src/api.cpp',
        ],
        if (tresD) ...const [
          'src/importador.cpp',
          'src/cena_3d.cpp',
          'src/renderizador_3d.cpp',
          'src/api_3d.cpp',
        ],
        if (tresD) ...Fontes3D.diligent(android: android, ios: ios),
        if (tresD) ...Fontes3D.assimp(),
      ],
      includes: [
        'src',
        if (tresD) ...Fontes3D.includes(android: android, apple: apple),
      ],
      defines: {if (tresD) ...Fontes3D.defines(android: android, ios: ios)},
      // `vulkan` E `android`: a sonda sobe o Vulkan de verdade e usa o
      // `__android_log_print`. Em plataforma que nao seja Android o
      // arquivo compila pelo ramo `#else` e nao puxa biblioteca nenhuma.
      libraries: [
        if (android) ...['vulkan', 'log', 'android'],
        if (ios) ...['MoltenVK', 'c++'],
        if (tresD) 'z',
      ],
      libraryDirectories: [if (ios) moltenVk],
      // No iOS, os dois arquivos de plataforma do Diligent sao Objective-C++
      // (.mm). As fontes .cpp continuam sendo inferidas como C++ pelo clang.
      language: ios ? Language.objectiveC : Language.cpp,
      frameworks: [
        if (ios) ...[
          'Metal',
          'QuartzCore',
          'UIKit',
          'CoreGraphics',
          'IOSurface',
          'Foundation',
        ],
      ],
      // C++20, e nao 23: `std::expected` e `std::format` nao existem em
      // todas as libc++ que o Aurea encontra (NDK 28 tem; o aparelho de
      // teste do dono nem sempre). `base.h` traz o `Resulta` proprio.
      std: 'c++20',
      flags: [
        if (input.config.code.targetOS == OS.windows) '/EHsc',
        if (ios) '-ObjC',
      ],
    ).run(input: input, output: output);

    // OS CABECALHOS TAMBEM SAO ENTRADA DO BUILD.
    //
    // O `CBuilder` so olha para os `.cpp` da lista: mexer num `.h` nao
    // invalidava o cache, e o binario continuava com o codigo velho — o
    // sintoma e o pior possivel, porque o fonte que esta na tela nao e o
    // que esta rodando. Aconteceu nesta sessao: um ajuste em
    // `avaliador_da_timeline.h` nao entrou, o teste falhou por um motivo
    // que o codigo ja tinha corrigido, e a cacada foi atras de um erro
    // que nao existia mais.
    //
    // `output.dependencies` e o mecanismo do proprio hook: qualquer
    // arquivo listado ali derruba o build quando muda depois dele.
    //
    // A URI TEM DE SER DE ARQUIVO, e nao `package:`. O `hooks` guarda a
    // lista em JSON e chama `toFilePath()`, que nao sabe ler uma URI de
    // pacote — e o erro que sai dali ("Cannot extract a file path from a
    // package URI") nao diz nada sobre o que estava sendo registrado.
    for (final arquivo in Directory('src').listSync()) {
      if (arquivo is! File || !arquivo.path.endsWith('.h')) continue;
      output.dependencies.add(File(arquivo.path).absolute.uri);
    }
  });
}
