import 'dart:ui' show ImageFilter;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/presentation/widgets/blend_mask.dart';

/// A mescla tem de alcancar o filho mesmo quando ele traz camadas do
/// motor (Opacity, ImageFiltered): e o caminho da foto. Aqui garante-se
/// que os dois caminhos pintam sem erro, inclusive com margem.
void main() {
  Widget cena(Widget filho) => MaterialApp(
        home: Scaffold(
          body: Center(
            child: SizedBox(
              width: 200,
              height: 200,
              child: Stack(children: [
                const Positioned.fill(child: ColoredBox(color: Colors.red)),
                Positioned.fill(child: filho),
              ]),
            ),
          ),
        ),
      );

  testWidgets('filho simples: saveLayer no canvas', (tester) async {
    await tester.pumpWidget(cena(const BlendMask(
      blendMode: BlendMode.plus,
      child: ColoredBox(color: Colors.blue),
    )));
    await tester.pump();
    expect(tester.takeException(), isNull);
  });

  testWidgets('filho com camadas: fotografa com margem e desenha com o modo',
      (tester) async {
    await tester.pumpWidget(cena(BlendMask(
      blendMode: BlendMode.plus,
      margem: 24,
      child: Opacity(
        opacity: 0.5,
        child: ImageFiltered(
          imageFilter: ImageFilter.blur(sigmaX: 4, sigmaY: 4),
          child: const ColoredBox(color: Colors.blue),
        ),
      ),
    )));
    await tester.pump();
    expect(tester.takeException(), isNull);
  });

  testWidgets('modo normal pinta direto, sem camada', (tester) async {
    await tester.pumpWidget(cena(const BlendMask(
      blendMode: BlendMode.srcOver,
      child: Opacity(opacity: 0.5, child: ColoredBox(color: Colors.blue)),
    )));
    await tester.pump();
    expect(tester.takeException(), isNull);
  });
}
