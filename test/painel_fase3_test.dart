import 'package:aurea/src/core/ds/ds.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

/// FASE 3 DO REDESIGN — O PAINEL CONTEXTUAL (docs/UI_REDESIGN_PLAN.md, 3.3).
///
/// A linha de propriedade da UI nova ([AureaPropertyRow]): tocar o numero
/// abre o teclado do app, que aceita "1080/3" e "50%" e prende o valor na
/// faixa da linha.
void main() {
  Future<void> digitar(WidgetTester tester, String texto) async {
    final campo = find.byKey(const ValueKey('valor-campo'));
    expect(campo, findsOneWidget, reason: 'o teclado de valor abriu');
    await tester.enterText(campo, texto);
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
  }

  testWidgets(
    'AureaPropertyRow: tocar o numero digita o valor, com conta e porcentagem',
    (tester) async {
      tester.view.physicalSize = const Size(430, 932);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      final recebidos = <double>[];
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: Column(
              children: [
                AureaPropertyRow(
                  rotulo: 'Opacidade',
                  chave: 'v1',
                  valor: 100,
                  min: 0,
                  max: 100,
                  unidade: '%',
                  casas: 0,
                  aoMudar: recebidos.add,
                ),
                AureaPropertyRow(
                  rotulo: 'Posicao X',
                  chave: 'v2',
                  valor: 10,
                  aoMudar: recebidos.add,
                ),
              ],
            ),
          ),
        ),
      );
      expect(find.text('100%'), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('valor-v1')));
      await tester.pumpAndSettle();
      await digitar(tester, '50%');
      expect(recebidos, [50.0]);

      await tester.tap(find.byKey(const ValueKey('valor-v2')));
      await tester.pumpAndSettle();
      await digitar(tester, '1080/3');
      expect(recebidos.last, 360.0);

      // Fora da faixa: preso ao limite.
      await tester.tap(find.byKey(const ValueKey('valor-v1')));
      await tester.pumpAndSettle();
      await digitar(tester, '250');
      expect(recebidos.last, 100.0);
    },
  );
}
