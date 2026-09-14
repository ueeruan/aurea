import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

void main(List<String> args) async {
  await build(args, (input, output) async {
    await CBuilder.library(
      name: 'aurea_timecore',
      assetName: 'aurea_timecore.dart',
      sources: [
        'src/easing.cpp',
        'src/curve.cpp',
        'src/mapping.cpp',
        'src/api.cpp',
      ],
      includes: ['src'],
      language: Language.cpp,
      std: 'c++17',
    ).run(input: input, output: output);
  });
}
