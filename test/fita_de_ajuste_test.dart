// A FITA E O CONTROLE MAIS TOCADO DO PAINEL NOVO, e nao tem texto
// nenhum: se ela quebrar, quebra calada. Os casos aqui sao os que
// ja quebraram na pratica — um gesto roubado pela rolagem, o valor
// partindo do lugar errado, um numero invalido vindo do projeto, e o
// rotulo que e a unica maneira de achar a fita na arvore.

import 'package:aurea/src/features/editor/presentation/widgets/fita_de_ajuste.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

Widget _monta({
  required double valor,
  required void Function(double) aoMudar,
  VoidCallback? aoComecar,
  VoidCallback? aoTerminar,
  double porPixel = 1,
}) => MaterialApp(
  home: Scaffold(
    body: Center(
      child: SizedBox(
        width: 300,
        child: FitaDeAjuste(
          rotulo: 'Escala',
          valor: valor,
          porPixel: porPixel,
          aoComecar: aoComecar,
          aoMudar: aoMudar,
          aoTerminar: aoTerminar,
        ),
      ),
    ),
  ),
);

/// UM PAI QUE ARREDONDA, como o motor de verdade faz. E o unico jeito de
/// provar que o gesto congela o valor de partida: se ele relesse o valor
/// do widget a cada quadro, este arredondamento voltaria para dentro da
/// conta e o total sairia maior que o caminho que o dedo andou.
class _PaiQueArredonda extends StatefulWidget {
  const _PaiQueArredonda({required this.aoMudar});

  final void Function(double) aoMudar;

  @override
  State<_PaiQueArredonda> createState() => _PaiQueArredondaState();
}

class _PaiQueArredondaState extends State<_PaiQueArredonda> {
  double _valor = 100;

  @override
  Widget build(BuildContext context) => MaterialApp(
    home: Scaffold(
      body: Center(
        child: SizedBox(
          width: 300,
          child: FitaDeAjuste(
            rotulo: 'Escala',
            valor: _valor,
            porPixel: .3,
            aoMudar: (v) {
              widget.aoMudar(v);
              setState(() => _valor = v.roundToDouble());
            },
          ),
        ),
      ),
    ),
  );
}

void main() {
  testWidgets('rolagem que rouba o dedo nao fecha lote nao aberto', (t) async {
    // O Flutter avisa o cancelamento de um arrasto que nunca comecou.
    // Sem guarda, este arrasto vertical chamaria `aoTerminar` sozinho —
    // um `endGesture()` sem `beginGesture()`, e com dois dedos nas duas
    // fitas do par ele fecharia o lote do arrasto da outra.
    var comecos = 0;
    var fins = 0;
    await t.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: ListView(
            children: [
              // A LISTA E PREGUICOSA: com um vao maior que a tela, a
              // fita nunca chega a ser construida e o teste passa a
              // testar nada.
              const SizedBox(height: 200),
              FitaDeAjuste(
                rotulo: 'Escala',
                valor: 100,
                porPixel: .5,
                aoComecar: () => comecos++,
                aoMudar: (_) {},
                aoTerminar: () => fins++,
              ),
              const SizedBox(height: 900),
            ],
          ),
        ),
      ),
    );
    await t.drag(find.byType(FitaDeAjuste), const Offset(0, -120));
    await t.pumpAndSettle();
    expect(comecos, 0, reason: 'a rolagem venceu a arena');
    expect(fins, 0, reason: 'cancelar sem comecar nao pode fechar lote');
  });

  testWidgets('cada pixel de dedo vale um passo', (t) async {
    final vistos = <double>[];
    var comecos = 0;
    var fins = 0;
    await t.pumpWidget(
      _monta(
        valor: 100,
        porPixel: .5,
        aoComecar: () => comecos++,
        aoMudar: vistos.add,
        aoTerminar: () => fins++,
      ),
    );
    await t.drag(find.byType(FitaDeAjuste), const Offset(40, 0));
    await t.pumpAndSettle();
    expect(comecos, 1);
    expect(fins, 1);
    expect(vistos.last, closeTo(120, .001));
  });

  testWidgets('o arrasto parte do valor congelado, e nao do de volta', (
    t,
  ) async {
    final vistos = <double>[];
    await t.pumpWidget(_PaiQueArredonda(aoMudar: vistos.add));
    final gesto = await t.startGesture(t.getCenter(find.byType(FitaDeAjuste)));
    for (var i = 0; i < 8; i++) {
      await gesto.moveBy(const Offset(5, 0));
      await t.pump();
    }
    await gesto.up();
    await t.pumpAndSettle();
    // 40 px de dedo a 0,3 por pixel: 112, e nao os 116 que a soma dos
    // valores arredondados daria.
    expect(vistos.last, closeTo(112, .001));
  });

  testWidgets('valor quebrado nao contamina o arrasto', (t) async {
    final vistos = <double>[];
    await t.pumpWidget(_monta(valor: double.nan, aoMudar: vistos.add));
    await t.drag(find.byType(FitaDeAjuste), const Offset(30, 0));
    await t.pumpAndSettle();
    expect(vistos, isNotEmpty);
    expect(vistos.every((v) => v.isFinite), isTrue);
  });

  testWidgets('um passo inteiro de rolagem REPINTA a fita', (t) async {
    // O pintor antigo so olhava a fase dentro do passo de 9 px — certo
    // enquanto os riscos eram todos iguais. Com um forte a cada cinco,
    // dois valores a 9 px um do outro desenham diferente; se o
    // `shouldRepaint` voltar a comparar so a fase, a fita congela a cada
    // passo cheio e o forte da saltos.
    CustomPainter pintor() => t
        .widget<CustomPaint>(
          find.descendant(
            of: find.byType(FitaDeAjuste),
            matching: find.byType(CustomPaint),
          ),
        )
        .painter!;
    await t.pumpWidget(_monta(valor: 100, aoMudar: (_) {}));
    final antes = pintor();
    await t.pumpWidget(_monta(valor: 109, aoMudar: (_) {}));
    expect(pintor().shouldRepaint(antes), isTrue);
    final mesmo = pintor();
    await t.pumpWidget(_monta(valor: 109, aoMudar: (_) {}));
    expect(pintor().shouldRepaint(mesmo), isFalse);
  });

  testWidgets('a fita tem rotulo de acessibilidade', (t) async {
    await t.pumpWidget(_monta(valor: 1, aoMudar: (_) {}));
    // "AJUSTAR ESCALA", e nao "Escala": o chip da linha de parametro ja
    // leva o nome cru, e dois nos com o mesmo rotulo deixam quem le a
    // tela sem saber em qual dos dois esta encostando.
    expect(find.bySemanticsLabel('Ajustar Escala'), findsOneWidget);
  });
}
