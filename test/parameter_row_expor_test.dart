// EXPOR PELO TOQUE LONGO NO NOME (o item que faltava do plano de
// redesign, §10): com so o reset, o toque longo reseta DIRETO como
// sempre (nenhum menu no caminho de quem ja usava); com "expor" junto,
// vira um menu curto de duas acoes.
import 'package:aurea/src/features/editor/presentation/context/parameter_row.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  Widget host(Widget row) =>
      MaterialApp(home: Scaffold(body: Center(child: row)));

  testWidgets('so reset: o toque longo reseta direto, sem menu', (
    tester,
  ) async {
    var resets = 0;
    await tester.pumpWidget(
      host(
        ParameterRow(
          label: 'Giro',
          value: 45,
          min: 0,
          max: 360,
          onChanged: (_) {},
          onReset: () => resets++,
        ),
      ),
    );
    await tester.longPress(find.text('Giro'));
    await tester.pumpAndSettle();
    expect(resets, 1);
    expect(find.text('Resetar propriedade'), findsNothing);
  });

  testWidgets('reset + expor: menu curto, e "Expor no projeto" expoe', (
    tester,
  ) async {
    var resets = 0, expostos = 0;
    await tester.pumpWidget(
      host(
        ParameterRow(
          label: 'Opacidade',
          value: 80,
          min: 0,
          max: 100,
          onChanged: (_) {},
          onReset: () => resets++,
          onExpose: () => expostos++,
        ),
      ),
    );
    await tester.longPress(find.text('Opacidade'));
    await tester.pumpAndSettle();
    expect(find.text('Resetar propriedade'), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('expor-propriedade')));
    await tester.pumpAndSettle();
    expect(expostos, 1);
    expect(resets, 0);
  });

  testWidgets('menu: "Resetar propriedade" reseta e nada mais', (
    tester,
  ) async {
    var resets = 0, expostos = 0;
    await tester.pumpWidget(
      host(
        ParameterRow(
          label: 'Escala',
          value: 100,
          min: 0,
          max: 400,
          onChanged: (_) {},
          onReset: () => resets++,
          onExpose: () => expostos++,
        ),
      ),
    );
    await tester.longPress(find.text('Escala'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Resetar propriedade'));
    await tester.pumpAndSettle();
    expect(resets, 1);
    expect(expostos, 0);
  });
}
