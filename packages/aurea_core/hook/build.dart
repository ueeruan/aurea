import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

void main(List<String> args) async {
  await build(args, (input, output) async {
    // O Flutter tambem executa hooks numa passagem que nao constroi code
    // assets (por exemplo ao preparar o hot reload). Nessa passagem acessar
    // `config.code` e invalido e derrubava o app logo depois da instalacao.
    if (!input.config.buildCodeAssets) return;

    final android = input.config.code.targetOS == OS.android;
    await CBuilder.library(
      name: 'aurea_core',
      assetName: 'aurea_core.dart',
      sources: [
        'src/cor.cpp',
        'src/api.cpp',
        if (android) 'src/codificador_android.cpp',
      ],
      includes: ['src'],
      libraries: [if (android) ...['mediandk', 'log']],
      language: Language.cpp,
      std: 'c++17',
      flags: [if (input.config.code.targetOS == OS.windows) '/EHsc'],
    ).run(input: input, output: output);
  });
}
