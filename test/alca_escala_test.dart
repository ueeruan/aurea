import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio/abrir_editor.dart' show openEditor;

/// A ALCA DE ESCALA ACOMPANHA O DEDO — sempre no mesmo sentido.
///
/// O relato do beta: "dá ghost no zoom e não dá zoom de verdade". A conta
/// da alça dividia a distância do dedo pela meia diagonal da caixa JÁ
/// ESCALADA: cada escala aplicada aumentava a caixa, o evento seguinte
/// dividia por um número maior e devolvia uma escala menor, e o objeto
/// tremia entre dois tamanhos sem acompanhar o dedo. O teste antigo só
/// conferia "aumentou" depois de um arrasto; este confere que cada passo
/// para fora aumenta e cada passo para dentro diminui — que é o que a
/// conta contra a caixa BASE garante, e a contra a caixa escalada não.
void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  testWidgets('afastar o dedo aumenta a cada passo; aproximar diminui', (
    tester,
  ) async {
    final c = await openEditor(tester);
    await tester.tapAt(tester.getRect(find.byType(PreviewStage)).center);
    await tester.pumpAndSettle();
    final id = c.read(selectedLayerProvider)!;
    double escala() => c
        .read(editorControllerProvider)
        .layerById(id)!
        .scaleX
        .valueAt(Duration.zero);

    final alca = tester.getRect(find.byKey(const ValueKey('alca-escala'))).center;
    final gesto = await tester.startGesture(alca);
    await tester.pump();
    var anterior = escala();
    // Três passos para fora, cada um afastando o dedo do centro.
    for (var i = 0; i < 3; i++) {
      await gesto.moveBy(const Offset(14, 14));
      await tester.pump();
      final agora = escala();
      expect(agora, greaterThan(anterior), reason: 'passo $i para fora');
      anterior = agora;
    }
    // Dois passos de volta: a escala tem de cair, e nao tremer.
    for (var i = 0; i < 2; i++) {
      await gesto.moveBy(const Offset(-14, -14));
      await tester.pump();
      final agora = escala();
      expect(agora, lessThan(anterior), reason: 'passo $i para dentro');
      anterior = agora;
    }
    await gesto.up();
    await tester.pumpAndSettle();
  });

  testWidgets('a escala e funcao da distancia, nao do historico do gesto', (
    tester,
  ) async {
    // Dois gestos que terminam no MESMO ponto tem de dar a mesma escala,
    // mesmo que um tenha ido e voltado. Com a conta contra a caixa
    // escalada, o caminho percorrido mudava o resultado.
    final c = await openEditor(tester);
    await tester.tapAt(tester.getRect(find.byType(PreviewStage)).center);
    await tester.pumpAndSettle();
    final id = c.read(selectedLayerProvider)!;
    double escala() => c
        .read(editorControllerProvider)
        .layerById(id)!
        .scaleX
        .valueAt(Duration.zero);
    final escalaInicial = escala();
    final alca = tester.getRect(find.byKey(const ValueKey('alca-escala'))).center;

    final direto = await tester.startGesture(alca);
    await tester.pump();
    await direto.moveBy(const Offset(30, 30));
    await tester.pump();
    final escalaDireta = escala();
    await direto.up();
    await tester.pumpAndSettle();

    // Volta ao tamanho de antes para o segundo gesto partir do mesmo
    // lugar — pela API, e nao pelo desfazer, que tambem mexe na selecao e
    // no historico do gesto: o que se quer aqui e so a geometria igual.
    c.read(editorControllerProvider.notifier).editScaleUniform(
      id,
      Duration.zero,
      escalaInicial,
    );
    await tester.pumpAndSettle();
    expect(escala(), closeTo(escalaInicial, 1e-9));
    final alca2 = tester.getRect(find.byKey(const ValueKey('alca-escala'))).center;
    expect(alca2, alca, reason: 'mesma geometria, mesma alca');
    final zigue = await tester.startGesture(alca2);
    await tester.pump();
    await zigue.moveBy(const Offset(50, 50));
    await tester.pump();
    await zigue.moveBy(const Offset(-20, -20));
    await tester.pump();
    final escalaZigue = escala();
    await zigue.up();
    await tester.pumpAndSettle();

    expect(escalaZigue, closeTo(escalaDireta, 0.02));
  });
}
