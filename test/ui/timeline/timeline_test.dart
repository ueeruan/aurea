import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/keyframe_clipboard.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/presentation/ui/curva/curva.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:aurea/src/features/editor/presentation/ui/timeline/geometria.dart';
import 'package:aurea/src/features/editor/presentation/ui/timeline/linha_da_camada.dart';
import 'package:aurea/src/features/editor/presentation/ui/timeline/pintor_da_linha.dart';
import 'package:aurea/src/features/editor/presentation/ui/timeline/timeline.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'montagem.dart';

const _us = 1000000;

/// O pintor da linha de [id] (a linha principal da camada).
PintorDaLinha _pintorDaLinha(WidgetTester tester, String id) {
  final pinturas = tester.widgetList<CustomPaint>(
    find.descendant(
      of: find.byKey(ValueKey('linha-$id')),
      matching: find.byType(CustomPaint),
    ),
  );
  return pinturas.map((p) => p.painter).whereType<PintorDaLinha>().single;
}

void main() {
  group('medidas', () {
    testWidgets('regua 42, linha 28, clipe 23 (recuo 2,5), cabecalho 70', (
      tester,
    ) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final id = b.projeto.layers.single.id;
      expect(
        tester.getSize(find.byKey(const ValueKey('timeline-regua'))).height,
        42,
      );
      expect(tester.getSize(find.byKey(ValueKey('linha-$id'))).height, 28);
      expect(
        tester.getSize(find.byKey(ValueKey('cabecalho-$id'))),
        const Size(70, 28),
      );
      final caixa = GeometriaDaLinha.caixa(0, 100);
      expect(caixa.height, 23);
      expect(caixa.top, 2.5);
      // O cabecote e a linha de 1,5 no centro.
      final cabecote = tester.getRect(
        find.byKey(const ValueKey('timeline-cabecote')),
      );
      expect(cabecote.width, 1.5);
      expect(cabecote.center.dx, larguraDaBancada / 2);
      expect(
        tester.getSize(
          find.byKey(const ValueKey('timeline-toque-do-cabecote')),
        ),
        const Size(100, 38),
      );
      expect(tester.takeException(), isNull);
    });
  });

  group('gestos', () {
    testWidgets('tocar no clipe escolhe a camada; tocar no vazio solta', (
      tester,
    ) async {
      var soltou = 0;
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
        aoTocarNoVazio: () => soltou++,
      );
      final id = b.projeto.layers.single.id;
      await tester.tapAt(pontoNoTempo(tester, b, id, 0.5));
      await tester.pumpAndSettle();
      expect(b.container.read(selectedLayerProvider), id);
      // Antes do comeco do clipe (e fora do cabecalho) e vazio.
      await tester.tapAt(pontoNoTempo(tester, b, id, -0.6));
      await tester.pumpAndSettle();
      expect(soltou, 1);
    });

    testWidgets('arrastar o clipe escolhido move no tempo, e e UM desfazer', (
      tester,
    ) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final id = b.projeto.layers.single.id;
      await tester.tapAt(pontoNoTempo(tester, b, id, 0.5));
      await tester.pumpAndSettle();
      // Cinco passos com quadros no meio: o gesto nao se parte.
      final g = await tester.startGesture(pontoNoTempo(tester, b, id, 0.5));
      for (var i = 0; i < 5; i++) {
        await g.moveBy(const Offset(20, 0));
        await tester.pump(const Duration(milliseconds: 600));
      }
      await g.up();
      await tester.pumpAndSettle();
      final l = b.projeto.layerById(id)!;
      expect(l.startTime, const Duration(seconds: 1));
      expect(l.duration, const Duration(seconds: 3));
      b.c.undo();
      await tester.pumpAndSettle();
      expect(b.projeto.layerById(id)!.startTime, Duration.zero);
    });

    testWidgets('clipe NAO escolhido nao se arrasta: o arrasto e scrub', (
      tester,
    ) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final id = b.projeto.layers.single.id;
      await tester.dragFrom(
        pontoNoTempo(tester, b, id, 1),
        const Offset(-100, 0),
      );
      await tester.pumpAndSettle();
      expect(b.projeto.layerById(id)!.startTime, Duration.zero);
      expect(b.playback.time.value, greaterThan(Duration.zero));
    });

    testWidgets('arrastar a alca apara a ponta (fora do clipe)', (
      tester,
    ) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final id = b.projeto.layers.single.id;
      // O fim (3 s) entra na tela com o cabecote em 2 s.
      b.playback.seek(const Duration(seconds: 2));
      await tester.pumpAndSettle();
      await tester.tapAt(pontoNoTempo(tester, b, id, 2.2));
      await tester.pumpAndSettle();
      expect(find.byKey(ValueKey('alcas-$id')), findsOneWidget);
      // 15 dp DEPOIS do fim: a alca de 30 de toque fora do clipe.
      final alca = pontoNoTempo(tester, b, id, 3) + const Offset(15, 0);
      final g = await tester.startGesture(alca);
      await g.moveBy(const Offset(-25, 0));
      await tester.pump();
      await g.moveBy(const Offset(-25, 0));
      await tester.pump();
      await g.up();
      await tester.pumpAndSettle();
      final l = b.projeto.layerById(id)!;
      expect(l.startTime, Duration.zero);
      expect(l.endTime, const Duration(milliseconds: 2500));
      // Um desfazer devolve os 3 s.
      b.c.undo();
      expect(b.projeto.layerById(id)!.endTime, const Duration(seconds: 3));
    });

    testWidgets('o ima gruda no cabecote (com guia) ao mover o clipe', (
      tester,
    ) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final id = b.projeto.layers.single.id;
      b.playback.seek(const Duration(seconds: 1));
      await tester.pumpAndSettle();
      await tester.tapAt(pontoNoTempo(tester, b, id, 1.5));
      await tester.pumpAndSettle();
      // 95 dp = 0,95 s: a 5 dp do cabecote, dentro dos 12 do ima.
      final g = await tester.startGesture(pontoNoTempo(tester, b, id, 1.5));
      await g.moveBy(const Offset(50, 0));
      await tester.pump();
      await g.moveBy(const Offset(45, 0));
      await tester.pump();
      expect(b.estado(tester).guiaUs.value, 1 * _us);
      await g.up();
      await tester.pumpAndSettle();
      expect(b.projeto.layerById(id)!.startTime, const Duration(seconds: 1));
      expect(b.estado(tester).guiaUs.value, isNull);
    });

    testWidgets('reordenar pela alca muda a ordem, o Z do palco e e UM '
        'desfazer', (tester) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'A');
          c.addShapeLayer(Duration.zero, name: 'B');
          c.addShapeLayer(Duration.zero, name: 'C');
        },
      );
      // A lista do projeto: indice 0 = a da FRENTE (a ultima adicionada).
      final antes = [for (final l in b.projeto.layers) l.id];
      final frente = antes.first;
      // A linha de cima e a da frente.
      expect(
        centroDaLinha(tester, frente).dy,
        lessThan(centroDaLinha(tester, antes[1]).dy),
      );
      await tester.tapAt(pontoNoTempo(tester, b, frente, 0.5));
      await tester.pumpAndSettle();
      final alca = find.byKey(ValueKey('alca-reordenar-$frente'));
      expect(tester.getSize(alca).width, AureaDims.alcaDeReordenar);
      // Duas linhas para baixo: a da frente vai para o fundo da pilha.
      final g = await tester.startGesture(tester.getCenter(alca));
      await g.moveBy(const Offset(0, 20));
      await tester.pump();
      await g.moveBy(const Offset(0, 36));
      await tester.pump();
      // O traco de destino aparece enquanto o dedo segura.
      expect(b.estado(tester).destinoDoReordenar.value, isNotNull);
      await g.up();
      await tester.pumpAndSettle();
      final depois = [for (final l in b.projeto.layers) l.id];
      expect(depois, [antes[1], antes[2], frente]);
      // A timeline acompanha: a linha dela agora e a de baixo.
      expect(
        centroDaLinha(tester, frente).dy,
        greaterThan(centroDaLinha(tester, antes[2]).dy),
      );
      expect(b.estado(tester).destinoDoReordenar.value, isNull);
      b.c.undo();
      expect([for (final l in b.projeto.layers) l.id], antes);
    });

    testWidgets('toque longo no cabecalho + arrastar tambem reordena', (
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
      final g = await tester.startGesture(
        tester.getCenter(find.byKey(ValueKey('cabecalho-${antes.first}'))) -
            const Offset(20, 0),
      );
      await tester.pump(const Duration(milliseconds: 600));
      await g.moveBy(const Offset(0, 28));
      await tester.pump();
      await g.up();
      await tester.pumpAndSettle();
      expect([for (final l in b.projeto.layers) l.id], antes.reversed.toList());
    });

    testWidgets('pinca: zoom ancorado no instante sob os dedos', (
      tester,
    ) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final e = b.estado(tester);
      const foco = 275.0;
      final antes = e.tempoDoX(foco);
      final p1 = await tester.startGesture(const Offset(foco - 25, 30));
      final p2 = await tester.startGesture(const Offset(foco + 25, 30));
      // Passos simetricos de meio dp, o de fora por ultimo: a arena aceita
      // a pinca com o foco exatamente em 275.
      for (var i = 0; i < 50; i++) {
        await p1.moveBy(const Offset(-.5, 0));
        await p2.moveBy(const Offset(.5, 0));
        await tester.pump();
      }
      // O zoom conta a partir de quando a arena reconhece a pinca (a folga
      // da escala): os dedos foram de ~86 a 100 dp depois disso.
      expect(e.pps.value, greaterThan(110));
      // O instante sob os dedos continua sob os dedos (menos de 1 dp).
      expect((e.tempoDoX(foco) - antes).abs(), lessThan(e.usPorPx(1)));
      await p1.up();
      await p2.up();
      await tester.pumpAndSettle();
      // Soltar so acomoda no quadro: nunca mais que meio quadro.
      expect((e.tempoDoX(foco) - antes).abs(), lessThanOrEqualTo(_us / 30));
      expect(tester.takeException(), isNull);
    });

    testWidgets('arrastar no vazio faz scrub (o tempo corre sob o cabecote)', (
      tester,
    ) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final id = b.projeto.layers.single.id;
      // Antes do clipe (vazio): arrastar para a ESQUERDA anda para a frente.
      await tester.dragFrom(
        pontoNoTempo(tester, b, id, -0.5),
        const Offset(-100, 0),
      );
      await tester.pumpAndSettle();
      final t = b.playback.time.value.inMicroseconds;
      // A folga do arrasto (20 do teste) nao conta: 80 dp = 0,8 s (±1
      // quadro, e a inercia do fim pode empurrar um pouco).
      expect(t, greaterThanOrEqualTo(800000 - _us ~/ 30));
      expect(b.estado(tester).vistaUs.value, t.toDouble());
      // E para a DIREITA volta.
      await tester.dragFrom(
        pontoNoTempo(tester, b, id, -0.5),
        const Offset(60, 0),
      );
      await tester.pumpAndSettle();
      expect(b.playback.time.value.inMicroseconds, lessThan(t));
    });

    testWidgets('arrastar na vertical rola as camadas', (tester) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) {
          for (var i = 0; i < 30; i++) {
            c.addShapeLayer(Duration.zero);
          }
        },
      );
      final primeira = b.projeto.layers.first.id;
      final y0 = centroDaLinha(tester, primeira).dy;
      await tester.dragFrom(const Offset(120, 150), const Offset(0, -100));
      await tester.pumpAndSettle();
      expect(b.playback.time.value, Duration.zero);
      final sobe = find.byKey(ValueKey('linha-$primeira'));
      if (sobe.evaluate().isNotEmpty) {
        expect(tester.getCenter(sobe).dy, lessThan(y0));
      }
    });
  });

  group('menus e bordas', () {
    testWidgets('toque longo no clipe abre o menu da camada', (tester) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final id = b.projeto.layers.single.id;
      await tester.longPressAt(pontoNoTempo(tester, b, id, 0.5));
      await tester.pumpAndSettle();
      expect(
        find.byWidgetPredicate(
          (w) => w.runtimeType.toString().startsWith('AureaMenu<'),
        ),
        findsOneWidget,
      );
      // O menu e daquela camada: ela ficou escolhida.
      expect(b.container.read(selectedLayerProvider), id);
    });

    testWidgets('toque longo no losango abre o editor de curva', (
      tester,
    ) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final id = b.projeto.layers.single.id;
      b.c
        ..toggleKeyframe(id, const Duration(seconds: 1), LayerProp.opacity)
        ..toggleKeyframe(id, const Duration(seconds: 2), LayerProp.opacity);
      b.container.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      await tester.longPressAt(
        pontoNoTempo(tester, b, id, 1, y: GeometriaDaLinha.yDoLosangoNoClipe),
      );
      await tester.pumpAndSettle();
      expect(find.byType(EditorDeCurva), findsOneWidget);
    });

    testWidgets('arrastar o clipe ate a borda rola o tempo sozinho '
        '(auto-rolagem) e o clipe acompanha o dedo', (tester) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final id = b.projeto.layers.single.id;
      await tester.tapAt(pontoNoTempo(tester, b, id, 0.5));
      await tester.pumpAndSettle();
      final g = await tester.startGesture(pontoNoTempo(tester, b, id, 0.55));
      await g.moveTo(
        Offset(larguraDaBancada - 5, centroDaLinha(tester, id).dy),
      );
      await tester.pump();
      final inicioAntes = b.projeto.layerById(id)!.startTime;
      for (var i = 0; i < 10; i++) {
        await tester.pump(const Duration(milliseconds: 50));
      }
      final andou = b.estado(tester).vistaUs.value;
      // 120 dp/s a 100 dp/s de zoom: ~1,2 s de tempo por segundo.
      expect(andou, greaterThan(300000));
      expect(
        b.projeto.layerById(id)!.startTime - inicioAntes,
        greaterThan(const Duration(milliseconds: 300)),
      );
      await g.up();
      await tester.pumpAndSettle();
      // Soltou: a rolagem para.
      final parado = b.playback.time.value;
      await tester.pump(const Duration(milliseconds: 200));
      expect(b.playback.time.value, parado);
    });
  });

  group('keyframes na timeline', () {
    testWidgets('losango: tocar escolhe, arrastar move (UM desfazer)', (
      tester,
    ) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero);
        },
      );
      final id = b.projeto.layers.single.id;
      b.c.toggleKeyframe(id, const Duration(seconds: 1), LayerProp.opacity);
      await tester.tapAt(pontoNoTempo(tester, b, id, 0.2));
      await tester.pumpAndSettle();
      final y = GeometriaDaLinha.yDoLosangoNoClipe;
      final losango = pontoNoTempo(tester, b, id, 1, y: y);
      await tester.tapAt(losango);
      await tester.pumpAndSettle();
      final sel = b.container.read(keyframesSelecionadosProvider);
      expect(
        marcaSelecionada(sel, (
          layerId: id,
          prop: LayerProp.opacity,
          tempo: const Duration(seconds: 1),
        )),
        isTrue,
      );
      // O cabecote foi ate a marca.
      expect(b.playback.time.value, const Duration(seconds: 1));
      // Arrastar leva a marca (o losango agora esta sob o cabecote).
      final aqui = pontoNoTempo(tester, b, id, 1, y: y);
      final g = await tester.startGesture(aqui);
      await g.moveBy(const Offset(25, 0));
      await tester.pump();
      await g.moveBy(const Offset(25, 0));
      await tester.pump();
      await g.up();
      await tester.pumpAndSettle();
      final l = b.projeto.layerById(id)!;
      expect(
        l.opacity.keyframes.map((k) => k.time),
        contains(const Duration(milliseconds: 1500)),
      );
      // A selecao acompanhou a marca.
      expect(
        marcaSelecionada(b.container.read(keyframesSelecionadosProvider), (
          layerId: id,
          prop: LayerProp.opacity,
          tempo: const Duration(milliseconds: 1500),
        )),
        isTrue,
      );
      b.c.undo();
      expect(
        b.projeto.layerById(id)!.opacity.keyframes.map((k) => k.time),
        contains(const Duration(seconds: 1)),
      );
    });

    testWidgets('propriedade ativa: os losangos dela acendem, os outros '
        'apagam; a camada expandida mostra uma linha por propriedade', (
      tester,
    ) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final id = b.projeto.layers.single.id;
      b.c.toggleKeyframe(
        id,
        const Duration(milliseconds: 500),
        LayerProp.position,
      );
      b.c.toggleKeyframe(id, const Duration(seconds: 1), LayerProp.opacity);
      b.container.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      // Sem propriedade em foco: tudo aceso.
      expect(_pintorDaLinha(tester, id).losangos!.acesos, isNull);
      b.container.read(propriedadeAtivaProvider.notifier).state =
          const PropriedadeAtiva.transformacao(LayerProp.opacity);
      await tester.pumpAndSettle();
      final acesos = _pintorDaLinha(tester, id).losangos!.acesos!;
      expect(acesos, contains(1 * _us));
      expect(acesos, isNot(contains(500000)));

      // Toque no cabecalho da camada escolhida e animada (fora do olho, que
      // ocupa os 44 da direita): abre as linhas.
      await tester.tapAt(
        tester.getTopLeft(find.byKey(ValueKey('cabecalho-$id'))) +
            const Offset(15, 14),
      );
      await tester.pumpAndSettle();
      expect(find.byKey(ValueKey('trilha-$id-position')), findsOneWidget);
      expect(find.byKey(ValueKey('trilha-$id-opacity')), findsOneWidget);
      PintorDaTrilha trilha(String nome) => tester
          .widgetList<CustomPaint>(
            find.descendant(
              of: find.byKey(ValueKey('trilha-$id-$nome')),
              matching: find.byType(CustomPaint),
            ),
          )
          .map((p) => p.painter)
          .whereType<PintorDaTrilha>()
          .single;
      expect(trilha('opacity').ativa, isTrue);
      expect(trilha('position').ativa, isFalse);
      expect(trilha('position').losangos.acesos, isEmpty);
      // Tocar no nome de outra propriedade poe ela em foco.
      await tester.tap(find.byKey(ValueKey('trilha-nome-$id-position')));
      await tester.pumpAndSettle();
      expect(
        b.container.read(propriedadeAtivaProvider),
        const PropriedadeAtiva.transformacao(LayerProp.position),
      );
      expect(trilha('position').ativa, isTrue);
    });

    testWidgets('na linha da propriedade, arrastar move SO a marca dela', (
      tester,
    ) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final id = b.projeto.layers.single.id;
      b.c.toggleKeyframe(id, const Duration(seconds: 1), LayerProp.position);
      b.c.toggleKeyframe(id, const Duration(seconds: 1), LayerProp.opacity);
      b.container.read(selectedLayerProvider.notifier).state = id;
      b.container.read(camadasExpandidasProvider.notifier).state = {id};
      await tester.pumpAndSettle();
      final linha = tester.getRect(find.byKey(ValueKey('trilha-$id-opacity')));
      final x = b.estado(tester).xDoTempo(1 * _us);
      final g = await tester.startGesture(Offset(x, linha.center.dy));
      await g.moveBy(const Offset(-25, 0));
      await tester.pump();
      await g.moveBy(const Offset(-25, 0));
      await tester.pump();
      await g.up();
      await tester.pumpAndSettle();
      final l = b.projeto.layerById(id)!;
      expect(
        l.opacity.keyframes.map((k) => k.time),
        contains(const Duration(milliseconds: 500)),
      );
      expect(
        l.position.keyframes.map((k) => k.time),
        contains(const Duration(seconds: 1)),
      );
    });
  });

  testWidgets('na linha do efeito, arrastar move SO a marca do efeito', (
    tester,
  ) async {
    final b = await montarTimeline(
      tester,
      preparar: (c) => c.addShapeLayer(Duration.zero),
    );
    final id = b.projeto.layers.single.id;
    b.c.addEffect(id, effectsInCategory('Color').first);
    final efeito = b.projeto.layerById(id)!.effects.single.id;
    b.c
      ..toggleEffectKeyframe(id, efeito, const Duration(seconds: 1))
      ..toggleKeyframe(id, const Duration(seconds: 1), LayerProp.opacity);
    b.container.read(selectedLayerProvider.notifier).state = id;
    b.container.read(camadasExpandidasProvider.notifier).state = {id};
    await tester.pumpAndSettle();
    final linha = tester.getRect(
      find.byKey(ValueKey('trilha-$id-efeito-$efeito')),
    );
    final x = b.estado(tester).xDoTempo(1 * _us);
    final g = await tester.startGesture(Offset(x, linha.center.dy));
    await g.moveBy(const Offset(25, 0));
    await tester.pump();
    await g.moveBy(const Offset(25, 0));
    await tester.pump();
    await g.up();
    await tester.pumpAndSettle();
    final l = b.projeto.layerById(id)!;
    expect(
      l.effects.single.keyframeTimes,
      contains(const Duration(milliseconds: 1500)),
    );
    expect(
      l.opacity.keyframes.map((k) => k.time),
      contains(const Duration(seconds: 1)),
    );
    // UM desfazer devolve a marca do efeito.
    b.c.undo();
    expect(
      b.projeto.layerById(id)!.effects.single.keyframeTimes,
      contains(const Duration(seconds: 1)),
    );
  });

  group('desempenho', () {
    testWidgets('o tique do relogio nao reconstroi as linhas nem a timeline', (
      tester,
    ) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) {
          for (var i = 0; i < 5; i++) {
            c.addShapeLayer(Duration.zero);
          }
        },
      );
      SondaDaTimeline.zerar();
      for (var i = 1; i <= 10; i++) {
        b.playback.seek(Duration(milliseconds: 100 * i));
        await tester.pump();
      }
      b.playback.play();
      for (var i = 0; i < 20; i++) {
        await tester.pump(const Duration(milliseconds: 16));
      }
      expect(b.playback.time.value, greaterThan(const Duration(seconds: 1)));
      // A vista andou junto (e o que os pintores desenham)...
      expect(
        b.estado(tester).vistaUs.value,
        b.playback.time.value.inMicroseconds.toDouble(),
      );
      b.playback.pause();
      await tester.pump();
      // ...e nada foi reconstruido.
      expect(SondaDaTimeline.buildsDeLinha, 0);
      expect(SondaDaTimeline.buildsDaTimeline, 0);
    });

    testWidgets('mover UM clipe reconstroi so a linha dele', (tester) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) {
          for (var i = 0; i < 5; i++) {
            c.addShapeLayer(Duration.zero);
          }
        },
      );
      final id = b.projeto.layers.first.id;
      SondaDaTimeline.zerar();
      b.c.moveLayer(id, const Duration(seconds: 1));
      await tester.pump();
      b.c.moveLayer(id, const Duration(seconds: 2));
      await tester.pump();
      expect(SondaDaTimeline.buildsDeLinha, 2);
      expect(SondaDaTimeline.buildsDaTimeline, 0);
    });

    testWidgets('com 100 camadas so as linhas da tela sao montadas', (
      tester,
    ) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) {
          for (var i = 0; i < 100; i++) {
            c.addShapeLayer(Duration.zero);
          }
        },
      );
      final montadas = find.byType(LinhaDaCamada).evaluate().length;
      // 238 dp de lista = 8,5 linhas, mais a folga de duas.
      expect(montadas, lessThanOrEqualTo(12));
      expect(montadas, greaterThanOrEqualTo(8));
      expect(
        find.byKey(ValueKey('linha-${b.projeto.layers.last.id}')),
        findsNothing,
      );
    });
  });

  testWidgets('dividir no cabecote: a timeline mostra os dois pedacos', (
    tester,
  ) async {
    final b = await montarTimeline(
      tester,
      preparar: (c) => c.addShapeLayer(Duration.zero),
    );
    final id = b.projeto.layers.single.id;
    b.container.read(selectedLayerProvider.notifier).state = id;
    b.playback.seek(const Duration(seconds: 1));
    await tester.pumpAndSettle();
    // O que o botao Dividir da barra faz.
    b.c.splitLayer(id, b.playback.time.value);
    await tester.pumpAndSettle();
    final camadas = b.projeto.layers;
    expect(camadas, hasLength(2));
    for (final l in camadas) {
      expect(find.byKey(ValueKey('linha-${l.id}')), findsOneWidget);
    }
    expect(camadas.map((l) => l.startTime).toSet(), {
      Duration.zero,
      const Duration(seconds: 1),
    });
  });
}
