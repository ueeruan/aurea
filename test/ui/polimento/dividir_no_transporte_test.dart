// DIVIDIR SEM ROLAR: era a nona ferramenta do video (dois arrastos na
// barra da camada). Agora a tesoura fica SEMPRE a vista na barra de
// transporte quando ha uma camada escolhida.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/editor_shell.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import '../paineis/apoio_editor.dart';

void main() {
  for (final largura in [360.0, 411.0]) {
    testWidgets('a ${largura.toInt()}: a tesoura aparece com a camada '
        'escolhida, sem rolar nada, e corta no cabecote (UM desfazer)', (
      tester,
    ) async {
      final c = await abrirEditorInteiro(
        tester,
        tamanho: Size(largura, 844),
        preparar: (c) => c.addShapeLayer(Duration.zero, name: 'Forma'),
      );
      final tesoura = find.byKey(const ValueKey('transporte-dividir'));
      // Sem camada escolhida, nada de tesoura.
      expect(tesoura, findsNothing);

      final id = c.read(editorControllerProvider).layers.single.id;
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      final playback = tester
          .widget<EditorShell>(find.byType(EditorShell))
          .playback;

      // Na barra de transporte, inteira na tela: um toque, sem rolar.
      expect(tesoura, findsOneWidget);
      final barra = tester.getRect(
        find.byKey(const ValueKey('barra-de-transporte')),
      );
      final r = tester.getRect(tesoura);
      expect(barra.contains(r.center), isTrue);
      expect(r.left, greaterThanOrEqualTo(0));
      expect(r.right, lessThanOrEqualTo(largura));
      expect(r.width, greaterThanOrEqualTo(39.5), reason: 'encolheu');
      // Nao cobre o play (que continua no centro).
      final play = tester.getRect(find.byKey(const ValueKey('transporte-play')));
      expect(r.right, lessThanOrEqualTo(play.left));
      expect(play.center.dx, closeTo(largura / 2, .5));

      // O cabecote em 0: nao ha o que cortar, a tesoura apaga.
      await tester.tap(tesoura);
      await tester.pumpAndSettle();
      expect(c.read(editorControllerProvider).layers, hasLength(1));

      // No meio da camada: corta.
      playback.seek(const Duration(seconds: 1));
      await tester.pumpAndSettle();
      await tester.tap(tesoura);
      await tester.pumpAndSettle();
      final camadas = c.read(editorControllerProvider).layers;
      expect(camadas, hasLength(2));
      expect(
        camadas.map((l) => l.startTime).toSet(),
        {Duration.zero, const Duration(seconds: 1)},
      );
      c.read(editorControllerProvider.notifier).undo();
      await tester.pumpAndSettle();
      expect(c.read(editorControllerProvider).layers, hasLength(1));
      expect(tester.takeException(), isNull);
    });
  }
}
