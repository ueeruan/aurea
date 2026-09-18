import 'editor_audit_helpers.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

/// O RELATO: "a rotacao ta bugada, tem o negocio de descer e subir e ai
/// fica impossivel usar, ele nao gira".
///
/// Num aparelho baixo o corpo do painel ROLA. Quem reconhece arrasto
/// vertical (a rolagem) aceita o gesto com 18 px de folga; o arrasto
/// livre do dial, so com 36. A rolagem ganhava sempre, e o dial recebia
/// meia duzia de pixels antes de perder o dedo.
///
/// Estes testes seguram os tres controles de arrastar do painel no
/// tamanho de tela em que o corpo rola.
void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  const pequena = Size(360, 640);

  Future<(dynamic, String)> abrirTransformar(WidgetTester tester) async {
    final c = await openEditor(tester, size: pequena);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    await tester.tap(find.text('Movimentação e transformação'));
    await tester.pumpAndSettle();
    return (c, id);
  }

  testWidgets('o dial gira com arrasto VERTICAL, mesmo com o painel rolando', (
    tester,
  ) async {
    final (c, id) = await abrirTransformar(tester);
    await selectTransformTool(tester, 'Girar');
    await tester.pumpAndSettle();

    final dial = find.byKey(const ValueKey('rotation-dial'));
    expect(dial, findsOneWidget);
    double giro() =>
        c.read(editorControllerProvider).layerById(id)!.rotation.base;
    expect(giro(), 0);

    // O dedo desce pela lateral do dial: o gesto e todo vertical, que era
    // exatamente o que a rolagem roubava.
    final r = tester.getRect(dial);
    final dedo = await tester.startGesture(
      Offset(r.center.dx + r.width * 0.3, r.center.dy - r.height * 0.15),
    );
    for (var i = 0; i < 8; i++) {
      await dedo.moveBy(const Offset(0, 10));
      await tester.pump(const Duration(milliseconds: 16));
    }
    await dedo.up();
    await tester.pumpAndSettle();
    expect(giro().abs(), greaterThan(5), reason: 'o dial girou de verdade');
  });

  testWidgets('o pad de mover leva a camada com arrasto vertical', (
    tester,
  ) async {
    final (c, id) = await abrirTransformar(tester);
    final pad = find.byKey(const ValueKey('position-drag-pad'));
    expect(pad, findsOneWidget);
    final antes = c.read(editorControllerProvider).layerById(id)!.position.base;

    final r = tester.getRect(pad);
    final dedo = await tester.startGesture(r.center);
    for (var i = 0; i < 6; i++) {
      await dedo.moveBy(const Offset(0, 8));
      await tester.pump(const Duration(milliseconds: 16));
    }
    await dedo.up();
    await tester.pumpAndSettle();
    final depois = c.read(editorControllerProvider).layerById(id)!.position.base;
    expect(depois.dy, greaterThan(antes.dy + 20), reason: 'desceu na tela');
  });

  testWidgets('o pad do pivo arrasta, e o toque duplo devolve ao centro', (
    tester,
  ) async {
    final (c, id) = await abrirTransformar(tester);
    await selectTransformTool(tester, 'Pivo');
    await tester.pumpAndSettle();
    final pad = find.byKey(const ValueKey('pivot-drag-pad'));
    expect(pad, findsOneWidget);

    final r = tester.getRect(pad);
    final dedo = await tester.startGesture(r.center);
    for (var i = 0; i < 5; i++) {
      await dedo.moveBy(const Offset(0, 9));
      await tester.pump(const Duration(milliseconds: 16));
    }
    await dedo.up();
    await tester.pumpAndSettle();
    expect(
      c.read(editorControllerProvider).layerById(id)!.pivot.base.dy,
      greaterThan(20),
    );

    await tester.tapAt(r.center);
    await tester.pump(const Duration(milliseconds: 60));
    await tester.tapAt(r.center);
    await tester.pumpAndSettle();
    expect(
      c.read(editorControllerProvider).layerById(id)!.pivot.base,
      Offset.zero,
      reason: 'toque duplo volta ao centro',
    );
  });
}
