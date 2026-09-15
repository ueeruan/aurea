import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

void main(List<String> args) async {
  await build(args, (input, output) async {
    await CBuilder.library(
      name: 'aurea_tracker2',
      assetName: 'aurea_tracker2.dart',
      sources: ['src/trilhas.cpp', 'src/resolver.cpp'],
      includes: ['src'],
      language: Language.cpp,
      std: 'c++17',
    ).run(input: input, output: output);
  });
}
