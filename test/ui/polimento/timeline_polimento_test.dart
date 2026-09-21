// POLIMENTO DA TIMELINE (visto no teste do fluxo completo no emulador):
//
//  1. o fim do clipe perto da borda direita ficava sob a alca de reordenar
//     (≡, 35 na ponta da linha escolhida) e o arrasto de aparar virava
//     scrub — logo depois de importar;
//  7. na timeline vazia, o cabecote (fixo no centro) cortava a frase
//     "Toque em + para adicionar...".
import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import '../timeline/montagem.dart';

/// Escolhe a unica camada (toque no meio do clipe) e leva o FIM dela a
/// [daBorda] dp da borda direita. Devolve o id e o x do fim na tela.
Future<(String, double)> _fimPertoDaBorda(
  WidgetTester tester,
  Bancada b, {
  required double daBorda,
}) async {
  final id = b.projeto.layers.single.id;
  await tester.tapAt(pontoNoTempo(tester, b, id, 0.5));
  await tester.pumpAndSettle();
  expect(b.container.read(selectedLayerProvider), id);
  final e = b.estado(tester);
  final fimUs = b.projeto.layerById(id)!.endTime.inMicroseconds;
  e.irPara(fimUs - (e.largura - daBorda - e.centro) / e.pps.value * 1e6);
  await tester.pumpAndSettle();
  final linha = tester.getRect(find.byKey(ValueKey('linha-$id')));
  final x1 = linha.left + e.xDoTempo(fimUs);
  // A vista cai na grade de quadros (30 fps a 100 dp/s = 3,3 dp).
  expect(linha.right - x1, closeTo(daBorda, 3.4));
  return (id, x1);
}

/// Arrasta de [de] por [dx] em passos, como um dedo.
Future<void> _arrastar(WidgetTester tester, Offset de, double dx) async {
  final g = await tester.startGesture(de);
  const passos = 5;
  for (var i = 0; i < passos; i++) {
    await g.moveBy(Offset(dx / passos, 0));
    await tester.pump(const Duration(milliseconds: 16));
  }
  await g.up();
  await tester.pumpAndSettle();
}

void main() {
  group('trim do fim com o clipe colado na borda direita', () {
    testWidgets('fim a 10 dp da borda: arrastar a alca do fim APARA (e nao '
        'vira scrub)', (tester) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final (id, x1) = await _fimPertoDaBorda(tester, b, daBorda: 10);
      final linha = tester.getRect(find.byKey(ValueKey('linha-$id')));

      // NADA DE REORDENAR SOBRE OS CLIPES: o ≡ mora no cabecalho de 70.
      final grip = find.byKey(ValueKey('alca-reordenar-$id'));
      expect(grip, findsOneWidget);
      expect(
        tester.getRect(grip).right,
        lessThanOrEqualTo(linha.left + AureaDims.cabecalhoDaCamada),
      );
      // O toque na zona da alca e da alca (o hit test chega nela).
      final alvo = Offset(x1 + 5, linha.center.dy);
      final caminho = tester.hitTestOnBinding(alvo);
      final alcas = tester.renderObject(find.byKey(ValueKey('alcas-$id')));
      expect(caminho.path.any((h) => h.target == alcas), isTrue);

      final antes = b.projeto.layerById(id)!;
      final vista = b.estado(tester).vistaUs.value;
      await _arrastar(tester, alvo, -50);
      final depois = b.projeto.layerById(id)!;
      expect(depois.startTime, antes.startTime);
      // 50 dp a 100 dp/s = 0,5 s a menos (sem ima perto: nenhum vizinho).
      expect(
        (antes.duration - depois.duration).inMilliseconds,
        closeTo(500, 40),
      );
      // Nao foi scrub: a vista nao andou.
      expect(b.estado(tester).vistaUs.value, vista);
      // E UM desfazer.
      b.c.undo();
      await tester.pumpAndSettle();
      expect(b.projeto.layerById(id)!.duration, antes.duration);
    });

    testWidgets('margem a direita = alca: com o fim a 4 dp da borda a zona '
        'entra no clipe ate completar 30 a vista', (tester) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final (id, x1) = await _fimPertoDaBorda(tester, b, daBorda: 4);
      final linha = tester.getRect(find.byKey(ValueKey('linha-$id')));
      final antes = b.projeto.layerById(id)!;
      // O dedo 20 dp DENTRO do fim ainda pega a alca, e nao o mover.
      await _arrastar(tester, Offset(x1 - 20, linha.center.dy), -40);
      final depois = b.projeto.layerById(id)!;
      expect(depois.startTime, antes.startTime, reason: 'moveu em vez de aparar');
      expect(depois.duration, lessThan(antes.duration));
    });

    testWidgets('o ≡ do cabecalho reordena no arrasto vertical', (
      tester,
    ) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'A');
          c.addShapeLayer(Duration.zero, name: 'B');
        },
      );
      final antes = [for (final l in b.projeto.layers) l.id];
      await tester.tapAt(pontoNoTempo(tester, b, antes.first, 0.5));
      await tester.pumpAndSettle();
      final g = await tester.startGesture(
        tester.getCenter(find.byKey(ValueKey('alca-reordenar-${antes.first}'))),
      );
      await g.moveBy(const Offset(0, 20));
      await tester.pump();
      await g.moveBy(const Offset(0, 16));
      await tester.pump();
      await g.up();
      await tester.pumpAndSettle();
      expect([for (final l in b.projeto.layers) l.id], antes.reversed.toList());
    });
  });

  testWidgets('timeline vazia: a dica nao cruza o cabecote', (tester) async {
    await montarTimeline(tester);
    final dica = tester.getRect(
      find.byKey(const ValueKey('timeline-dica-vazia')),
    );
    final cabecote = tester.getRect(
      find.byKey(const ValueKey('timeline-cabecote')),
    );
    expect(
      dica.left > cabecote.right || dica.right < cabecote.left,
      isTrue,
      reason: 'dica $dica x cabecote $cabecote',
    );
    expect(dica.right, lessThanOrEqualTo(larguraDaBancada));
    expect(tester.takeException(), isNull);
  });
}
