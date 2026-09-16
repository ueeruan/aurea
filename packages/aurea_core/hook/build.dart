import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

void main(List<String> args) async {
  await build(args, (input, output) async {
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
