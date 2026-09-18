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
        'src/relogio_do_quadro.cpp',
        'src/nucleo.cpp',
        'src/api.cpp',
      ],
      includes: const ['src'],
      libraries: [if (android) 'log'],
      language: Language.cpp,
      // C++20, e nao 23: `std::expected` e `std::format` nao existem em
      // todas as libc++ que o Aurea encontra (NDK 28 tem; o aparelho de
      // teste do dono nem sempre). `base.h` traz o `Resulta` proprio.
      std: 'c++20',
      flags: [if (input.config.code.targetOS == OS.windows) '/EHsc'],
    ).run(input: input, output: output);
  });
}
