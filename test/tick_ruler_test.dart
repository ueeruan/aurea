import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/core/ui/am_tick_ruler.dart' as origem;
import 'package:aurea/src/features/editor/presentation/am/am_widgets.dart';

/// A superficie de arrasto: o valor tem de seguir o DEDO, nao a
/// cadencia de reconstrucao de quem esta por cima.
void main() {
  test('a regua do core e a de am_widgets sao A MESMA classe', () {
    // Ja houve duas: a viva em `am_widgets.dart` e uma copia morta em
    // `core/ui/am_tick_ruler.dart`, com o sinal antigo (direita DIMINUIA).
    // Nome identico, sentido oposto. Agora a do core e a unica, e
    // `am_widgets.dart` so a reexporta.
    expect(AmTickRuler, same(origem.AmTickRuler));
    expect(AmArrastoDeValor, same(origem.AmArrastoDeValor));
  });

  testWidgets('varios eventos no mesmo quadro somam, nao se perdem',
      (tester) async {
    var valor = 0.0;
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: StatefulBuilder(
          builder: (context, setState) => AmTickRuler(
            value: valor,
            min: 0,
            max: 1,
            unitsPerPixel: 1 / 420,
            onChanged: (v) => setState(() => valor = v),
          ),
        ),
      ),
    ));

    final centro = tester.getCenter(find.byType(AmTickRuler));
    final gesto = await tester.startGesture(centro);
    // Tres movimentos de 100 px SEM pump entre eles: chegam no mesmo
    // quadro, como um arrasto rapido de verdade.
    // PARA A DIREITA AUMENTA. A regua seguia o dedo ao contrario (um
    // "papel" que se puxa para a esquerda); as fitas do painel sempre
    // andaram com o dedo, e duas reguas do mesmo painel indo para lados
    // opostos e a pior resposta possivel. Agora todas andam junto.
    await gesto.moveBy(const Offset(100, 0));
    await gesto.moveBy(const Offset(100, 0));
    await gesto.moveBy(const Offset(100, 0));
    await gesto.up();
    await tester.pump();

    expect(valor, closeTo(300 / 420, 1e-6));
  });

  testWidgets('o segundo gesto parte do valor que ficou', (tester) async {
    var valor = 10.0;
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: StatefulBuilder(
          builder: (context, setState) => AmTickRuler(
            value: valor,
            unitsPerPixel: 0.5,
            onChanged: (v) => setState(() => valor = v),
          ),
        ),
      ),
    ));
    final centro = tester.getCenter(find.byType(AmTickRuler));
    var g = await tester.startGesture(centro);
    await g.moveBy(const Offset(40, 0));
    await g.up();
    await tester.pump();
    expect(valor, closeTo(30, 1e-6));

    g = await tester.startGesture(centro);
    await g.moveBy(const Offset(-20, 0));
    await g.up();
    await tester.pump();
    expect(valor, closeTo(20, 1e-6));
  });

  testWidgets('respeita min e max', (tester) async {
    var valor = 0.9;
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: StatefulBuilder(
          builder: (context, setState) => AmTickRuler(
            value: valor,
            min: 0,
            max: 1,
            unitsPerPixel: 0.01,
            onChanged: (v) => setState(() => valor = v),
          ),
        ),
      ),
    ));
    final centro = tester.getCenter(find.byType(AmTickRuler));
    final g = await tester.startGesture(centro);
    await g.moveBy(const Offset(200, 0));
    await g.up();
    await tester.pump();
    expect(valor, 1.0);
  });
}
