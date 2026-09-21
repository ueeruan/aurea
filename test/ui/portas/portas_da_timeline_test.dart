import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import '../timeline/montagem.dart';

// AS PORTAS DA REGUA: o menu da marca (renomear, pintar, apagar), a marca
// que se arrasta (um arrasto = um desfazer) e as migalhas do grupo (voltar
// direto a um nivel de fora).

const _s = Duration(seconds: 1);

/// O ponto da bandeirinha da marca em [t] (a regua fica no alto da tela).
Offset _naMarca(WidgetTester tester, Bancada b, Duration t) =>
    Offset(b.estado(tester).xDoTempo(t.inMicroseconds), 8);

Future<void> _segurar(WidgetTester tester, Offset onde) async {
  await tester.longPressAt(onde);
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('segurar a marca: renomear, pintar e apagar — cada um um '
      'desfazer', (tester) async {
    final b = await montarTimeline(
      tester,
      preparar: (c) {
        c.addShapeLayer(Duration.zero, name: 'Forma');
        c.toggleMarker(_s);
      },
    );
    expect(b.projeto.markers.single.time, _s);

    // RENOMEAR.
    await _segurar(tester, _naMarca(tester, b, _s));
    await tester.tap(find.byKey(const ValueKey('menu-marca-renomear')));
    await tester.pumpAndSettle();
    await tester.enterText(find.byKey(const ValueKey('estudio-nome')), 'Drop');
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    expect(b.projeto.markers.single.label, 'Drop');

    // PINTAR.
    await _segurar(tester, _naMarca(tester, b, _s));
    await tester.tap(find.byKey(const ValueKey('menu-marca-cor')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('menu-marca-cor-3')));
    await tester.pumpAndSettle();
    expect(
      b.projeto.markers.single.color.toARGB32(),
      LayerLabel.palette[3].color.toARGB32(),
    );
    b.c.undo();
    expect(b.projeto.markers.single.label, 'Drop', reason: 'so a cor voltou');
    expect(
      b.projeto.markers.single.color.toARGB32(),
      isNot(LayerLabel.palette[3].color.toARGB32()),
    );

    // APAGAR.
    await tester.pumpAndSettle();
    await _segurar(tester, _naMarca(tester, b, _s));
    await tester.tap(find.byKey(const ValueKey('menu-marca-apagar')));
    await tester.pumpAndSettle();
    expect(b.projeto.markers, isEmpty);
    b.c.undo();
    expect(b.projeto.markers.single.time, _s);
    expect(tester.takeException(), isNull);
  });

  testWidgets('arrastar a marca leva a marca; UM desfazer devolve', (
    tester,
  ) async {
    final b = await montarTimeline(
      tester,
      preparar: (c) {
        c.addShapeLayer(Duration.zero, name: 'Forma');
        c.toggleMarker(_s);
      },
    );
    final pps = b.estado(tester).pps.value;
    final g = await tester.startGesture(_naMarca(tester, b, _s));
    for (var i = 0; i < 10; i++) {
      await g.moveBy(Offset(pps / 20, 0));
      await tester.pump(const Duration(milliseconds: 16));
    }
    await g.up();
    await tester.pumpAndSettle();
    final andou = b.projeto.markers.single.time;
    expect(andou, greaterThan(const Duration(milliseconds: 1300)));
    expect(andou, lessThan(const Duration(milliseconds: 1700)));
    // O cabecote nao andou: o arrasto era da marca, nao scrub.
    expect(b.playback.time.value, Duration.zero);
    b.c.undo();
    expect(b.projeto.markers.single.time, _s);
    expect(tester.takeException(), isNull);
  });

  testWidgets('migalhas: segurar o "sair do grupo" volta direto ao projeto', (
    tester,
  ) async {
    final b = await montarTimeline(
      tester,
      preparar: (c) {
        // Dois grupos, um dentro do outro, montados por fora (o grupo de
        // UMA camada nao aceita outro grupo; o de varias, sim).
        c.addShapeLayer(Duration.zero, name: 'A');
        c.addShapeLayer(Duration.zero, name: 'B');
        c.groupLayers([for (final l in c.projetoCompleto.layers) l.id]);
        c.addShapeLayer(Duration.zero, name: 'C');
        c.groupLayers([for (final l in c.projetoCompleto.layers) l.id]);
        c.enterGroup(c.projetoCompleto.layers.single.id);
      },
    );
    // La dentro, o grupo de dentro.
    b.c.enterGroup(b.projeto.layers.whereType<GroupLayer>().single.id);
    await tester.pumpAndSettle();
    expect(b.c.profundidadeDoGrupo, 2);
    await tester.longPress(
      find.byKey(const ValueKey('timeline-sair-do-grupo')),
    );
    await tester.pumpAndSettle();
    // O projeto (nivel 0) e o grupo de fora (nivel 1); o atual nao entra.
    expect(find.byKey(const ValueKey('menu-migalha-0')), findsOneWidget);
    expect(find.byKey(const ValueKey('menu-migalha-1')), findsOneWidget);
    expect(find.byKey(const ValueKey('menu-migalha-2')), findsNothing);
    await tester.tap(find.byKey(const ValueKey('menu-migalha-0')));
    await tester.pumpAndSettle();
    expect(b.c.profundidadeDoGrupo, 0);
    expect(tester.takeException(), isNull);
  });
}
