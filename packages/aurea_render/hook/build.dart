import 'dart:io';

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

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
    final android = input.config.code.targetOS == OS.android;
    await CBuilder.library(
      name: 'aurea_render',
      assetName: 'aurea_render.dart',
      sources: const [
        'src/compositor.cpp',
        'src/gerenciador_de_recursos.cpp',
        'src/gerenciador_de_shaders.cpp',
        'src/avaliador_da_timeline.cpp',
        'src/backend_vulkan.cpp',
        'src/relogio_do_quadro.cpp',
        'src/nucleo.cpp',
        'src/api.cpp',
      ],
      includes: const ['src'],
      // `vulkan` E `android`: a sonda sobe o Vulkan de verdade e usa o
      // `__android_log_print`. Em plataforma que nao seja Android o
      // arquivo compila pelo ramo `#else` e nao puxa biblioteca nenhuma.
      libraries: [if (android) ...['vulkan', 'log', 'android']],
      language: Language.cpp,
      // C++20, e nao 23: `std::expected` e `std::format` nao existem em
      // todas as libc++ que o Aurea encontra (NDK 28 tem; o aparelho de
      // teste do dono nem sempre). `base.h` traz o `Resulta` proprio.
      std: 'c++20',
      flags: [if (input.config.code.targetOS == OS.windows) '/EHsc'],
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
