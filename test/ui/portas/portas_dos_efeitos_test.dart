import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/efeitos.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import '../paineis/apoio_paineis.dart';

// AS PORTAS DO CARTAO DE EFEITO: o keyframe do efeito inteiro e o "Assar
// em keyframes" (no ⋯), e as acoes do Estudio do tempo que moram agora no
// cartao do Time Remap (inverter a curva, reverso a partir do cabecote,
// velocidade constante).

Future<void> _menu(WidgetTester tester, String efeitoId, String item) async {
  await tester.tap(find.byKey(ValueKey('efeito-$efeitoId-menu')));
  await tester.pumpAndSettle();
  await tester.tap(find.byKey(ValueKey('menu-efeito-$item')));
  await tester.pumpAndSettle();
}

Future<void> _ver(WidgetTester tester, Finder alvo) => tester.scrollUntilVisible(
  alvo,
  80,
  scrollable: find
      .descendant(
        of: find.byKey(const ValueKey('pilha-de-efeitos')),
        matching: find.byType(Scrollable),
      )
      .first,
);

void main() {
  testWidgets('⋯ Keyframe em todos os parâmetros: crava e tira, um '
      'desfazer cada', (tester) async {
    final tipo = effectsInCategory('Color').first;
    final (b, id) = await montarPainel(
      tester,
      preparar: (c) {
        c.addShapeLayer(Duration.zero, name: 'Forma');
        final id = c.projetoCompleto.layers.single.id;
        c.addEffect(id, tipo);
        return id;
      },
      painel: (id) => PainelEfeitos(layerId: id),
    );
    EffectInstance efeito() => b.camada(id).effects.single;
    final eid = efeito().id;
    expect(efeito().hasAnimation, isFalse);

    await _menu(tester, eid, 'keyframe');
    expect(efeito().hasKeyframeAt(Duration.zero), isTrue);
    // TODOS os parametros ganharam a marca.
    for (final k in efeito().spec.params.keys) {
      expect(efeito().track(k).hasKeyframeAt(Duration.zero), isTrue, reason: k);
    }
    // De novo: agora o item tira.
    await _menu(tester, eid, 'keyframe');
    expect(efeito().hasAnimation, isFalse);
    b.c.undo();
    expect(efeito().hasKeyframeAt(Duration.zero), isTrue);
    // Efeito de cor nao e procedural: nao oferece "Assar".
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(ValueKey('efeito-$eid-menu')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('menu-efeito-keyframe')), findsOneWidget);
    expect(find.byKey(const ValueKey('menu-efeito-assar')), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('⋯ Assar em keyframes: o tremor vira movimento e sai da '
      'pilha', (tester) async {
    final (b, id) = await montarPainel(
      tester,
      preparar: (c) {
        c.addShapeLayer(Duration.zero, name: 'Forma');
        final id = c.projetoCompleto.layers.single.id;
        c.addEffect(id, EffectType.tremor);
        return id;
      },
      painel: (id) => PainelEfeitos(layerId: id),
    );
    final eid = b.camada(id).effects.single.id;
    expect(b.camada(id).position.isAnimated, isFalse);
    await _menu(tester, eid, 'assar');
    expect(b.camada(id).effects, isEmpty);
    expect(b.camada(id).position.isAnimated, isTrue);
    // UM desfazer devolve o efeito e tira as marcas.
    b.c.undo();
    expect(b.camada(id).effects.single.type, EffectType.tremor);
    expect(b.camada(id).position.isAnimated, isFalse);
    // O aviso com "Desfazer" fecha sozinho.
    await tester.pump(const Duration(seconds: 5));
    expect(tester.takeException(), isNull);
  });

  testWidgets('Time Remap: inverter a curva, reverso a partir daqui e '
      'velocidade constante', (tester) async {
    final (b, id) = await montarPainel(
      tester,
      altura: 600,
      preparar: (c) {
        final v = videoDeTeste();
        abrirProjetoCom(c, [v]);
        c.addEffect(v.id, EffectType.timeRemap);
        return v.id;
      },
      painel: (id) => PainelEfeitos(layerId: id),
    );
    VideoLayer video() => b.camada(id) as VideoLayer;
    final remap = video().effects.single;
    expect(video().timeRemap, isNotNull);
    final span = video().timeRemap!.keyframes.last.value;
    // Abre o cartao.
    await tester.tap(find.byKey(ValueKey('efeito-${remap.id}-seta')));
    await tester.pumpAndSettle();

    // INVERTER: o clipe corre de tras para frente.
    final inverter = find.byKey(const ValueKey('time-remap-inverter'));
    await _ver(tester, inverter);
    await tester.tap(inverter);
    await tester.pumpAndSettle();
    expect(video().timeRemap!.keyframes.first.value, closeTo(span, 1e-6));
    expect(video().timeRemap!.keyframes.last.value, closeTo(0, 1e-6));
    b.c.undo();
    expect(video().timeRemap!.keyframes.first.value, closeTo(0, 1e-6));

    // REVERSO A PARTIR DO CABECOTE (em 2 s): antes fica, depois espelha.
    final playback = tester
        .widget<EscopoDoEditor>(find.byType(EscopoDoEditor))
        .playback;
    playback.seek(const Duration(seconds: 2));
    await tester.pumpAndSettle();
    final daqui = find.byKey(const ValueKey('time-remap-reverso-daqui'));
    await _ver(tester, daqui);
    await tester.tap(daqui);
    await tester.pumpAndSettle();
    final trilha = video().timeRemap!;
    expect(trilha.hasKeyframeAt(const Duration(seconds: 2)), isTrue);
    final meio = trilha.valueAt(const Duration(seconds: 2));
    // Depois do cabecote o tempo da fonte VOLTA.
    expect(trilha.keyframes.last.value, lessThan(meio));
    b.c.undo();
    expect(trilha.keyframes, isNot(video().timeRemap!.keyframes));

    // VELOCIDADE CONSTANTE: a curva sai, o efeito sai, a duracao fica.
    final duracao = video().duration;
    final constante = find.byKey(const ValueKey('time-remap-constante'));
    await _ver(tester, constante);
    await tester.tap(constante);
    await tester.pumpAndSettle();
    expect(video().timeRemap, isNull);
    expect(video().duration, duracao);
    b.c.undo();
    expect(video().timeRemap, isNotNull);
    expect(tester.takeException(), isNull);
  });
}
