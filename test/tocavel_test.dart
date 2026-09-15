// O TOCAVEL — o aperto vivo dos botoes — e a regra da arena de gestos:
// um Tocavel SEM acao tem de ser transparente ao toque. So de registrar
// onTapUp/onTapCancel ele ja disputa (e vence) o tap do pai; foi assim
// que enfeites dentro de tiles fizeram menus inteiros pararem de abrir.
import 'package:aurea/src/core/ui/tocavel.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('com onTap, toca; segurar chama onLongPress', (tester) async {
    var taps = 0, longos = 0;
    await tester.pumpWidget(
      MaterialApp(
        home: Center(
          child: Tocavel(
            onTap: () => taps++,
            onLongPress: () => longos++,
            child: const SizedBox(width: 60, height: 60),
          ),
        ),
      ),
    );
    await tester.tap(find.byType(Tocavel));
    await tester.pumpAndSettle();
    expect(taps, 1);
    await tester.longPress(find.byType(Tocavel));
    await tester.pumpAndSettle();
    expect(longos, 1);
  });

  testWidgets('inativo e TRANSPARENTE: o tap atravessa para o pai', (
    tester,
  ) async {
    var doPai = 0;
    await tester.pumpWidget(
      MaterialApp(
        home: Center(
          child: Tocavel(
            onTap: () => doPai++,
            child: SizedBox(
              width: 60,
              height: 60,
              // O enfeite: um Tocavel sem acao por cima do conteudo.
              child: Tocavel(
                child: Container(color: const Color(0xFF222222)),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.byType(Tocavel).first);
    await tester.pumpAndSettle();
    expect(doPai, 1, reason: 'o Tocavel sem acao roubou o toque do pai');
  });

  testWidgets('apertado encolhe; solto volta ao tamanho', (tester) async {
    await tester.pumpWidget(
      MaterialApp(
        home: Center(
          child: Tocavel(
            onTap: () {},
            child: const SizedBox(width: 60, height: 60),
          ),
        ),
      ),
    );
    final gesture = await tester.startGesture(
      tester.getCenter(find.byType(Tocavel)),
    );
    await tester.pump(const Duration(milliseconds: 120));
    final apertado = tester.widget<AnimatedScale>(
      find.byType(AnimatedScale),
    );
    expect(apertado.scale, lessThan(1));
    await gesture.up();
    await tester.pumpAndSettle();
    final solto = tester.widget<AnimatedScale>(find.byType(AnimatedScale));
    expect(solto.scale, 1);
  });
}
