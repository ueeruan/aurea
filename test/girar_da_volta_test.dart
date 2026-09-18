import 'editor_audit_helpers.dart';
import 'dart:math' as math;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

/// O RELATO DO BETA: "quando eu tento girar, ao inves de dar uma volta
/// completa ele volta do lugar que ta ate o zero; o girar nao faz mais
/// de 1x a volta".
///
/// O dial lia o dedo por atan2 (-180..180) e ESCREVIA esse angulo: ao
/// cruzar a esquerda o valor saltava de 180 para -180, e uma volta era o
/// teto. Este teste arrasta o dedo em circulo pelo dial de verdade e
/// exige que o valor cruze o limite sem saltar, passe de 360 e conte
/// voltas — e que os chips de volta inteira somem 360.
void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  testWidgets('o dial de Girar cruza o limite e passa de uma volta', (tester) async {
    final c = await openEditor(tester);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    await tester.tap(find.text('Movimentação e transformação'));
    await tester.pumpAndSettle();
    await selectTransformTool(tester, 'Girar');
    await tester.pumpAndSettle();

    final dial = find.byKey(const ValueKey('rotation-dial'));
    expect(dial, findsOneWidget);
    final rect = tester.getRect(dial);
    final centro = rect.center;
    final raio = math.min(rect.width, rect.height) / 2 - 16;
    Offset em(double graus) =>
        centro +
        Offset(math.cos(graus * math.pi / 180), math.sin(graus * math.pi / 180)) * raio;
    double giro() => c
        .read(editorControllerProvider)
        .layerById(id)!
        .rotation
        .valueAt(Duration.zero);

    // O DIAL E RELATIVO: o que conta e quanto o dedo girou desde que
    // pousou. O dedo pousa a 170 graus (camada em 0) e circula ate 200,
    // cruzando a esquerda: +30, sem salto para tras em nenhum passo.
    expect(giro(), 0);
    final dedo = await tester.startGesture(em(170));
    await tester.pump();
    var anterior = giro();
    for (var a = 175.0; a <= 200; a += 5) {
      await dedo.moveTo(em(a));
      await tester.pump();
      expect(giro(), greaterThanOrEqualTo(anterior - 1e-6),
          reason: 'saltou para tras em $a graus (era o bug)');
      anterior = giro();
    }
    expect(giro(), closeTo(30, 3));

    // Segue circulando uma volta inteira: 390, nao 30 — conta voltas.
    for (var a = 210.0; a <= 560; a += 10) {
      await dedo.moveTo(em(a));
      await tester.pump();
    }
    expect(giro(), closeTo(390, 3), reason: 'nao contou as voltas');
    await dedo.up();
    await tester.pumpAndSettle();

    // Os chips de volta inteira.
    await tester.tap(find.byKey(const ValueKey('rotation-turn-plus')));
    await tester.pump();
    expect(giro(), closeTo(750, 3));
    await tester.tap(find.byKey(const ValueKey('rotation-turn-minus')));
    await tester.pump();
    expect(giro(), closeTo(390, 3));

    // Um toque seco no dial poe o angulo tocado NA VOLTA em que a camada
    // esta: 390 e "30 graus, 1x"; tocar em 90 graus da 450, nao 90.
    await tester.tapAt(em(90));
    await tester.pumpAndSettle();
    expect(giro(), closeTo(450, 3));
  });
}
