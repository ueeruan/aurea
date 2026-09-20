// O ESTUDIO DO TEMPO na tela: as portas e os gestos que os testadores
// vao usar — criar keyframe no cabecote, tocar na linha, arrastar num
// undo so, congelar, reverso, presets e o modo de interpolacao.
import 'dart:math' as math;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/cut_ops.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/remapear_tempo.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/estudio_do_tempo.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

Future<(ProviderContainer, PlaybackController, String)> _abrir(
  WidgetTester tester, {
  bool reverso = false,
}) async {
  final v = VideoLayer(
    name: 'v',
    startTime: const Duration(seconds: 3),
    duration: const Duration(seconds: 4),
    sourceDuration: const Duration(seconds: 12),
    speed: reverso ? 1 : 2,
    reverse: reverso,
    sourcePath: 'x.mp4',
    position: AnimatedOffset(Offset.zero),
  );
  final c = ProviderContainer();
  addTearDown(c.dispose);
  c
      .read(editorControllerProvider.notifier)
      .openProject(
        VideoProject(name: 'p', createdAt: DateTime(2026), layers: [v]),
      );
  final pb = PlaybackController(
    vsync: tester,
    durationOf: () => const Duration(seconds: 10),
  );
  addTearDown(pb.dispose);
  pb.seek(const Duration(seconds: 4)); // local: 1 s
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: Scaffold(
          body: Center(
            child: SizedBox(
              width: 390,
              height: 560,
              child: EstudioDoTempo(layerId: v.id, playback: pb),
            ),
          ),
        ),
      ),
    ),
  );
  await tester.pump();
  return (c, pb, v.id);
}

VideoLayer _clipe(ProviderContainer c, String id) =>
    c.read(editorControllerProvider).layerById(id)! as VideoLayer;

/// A MESMA geometria do estudio (margens 44/8/8/18 e folga de 12%): onde
/// um par (segundos, valor) cai na tela, no grafo de valor.
Offset _noGrafico(
  WidgetTester tester,
  VideoLayer l,
  AnimatedDouble curva,
  double seg,
  double v,
) {
  final r = tester.getRect(find.byKey(const ValueKey('estudio-tempo-grafico')));
  final quadro = Rect.fromLTRB(
    r.left + 44,
    r.top + 8,
    r.right - 8,
    r.bottom - 18,
  );
  final dur = l.duration.inMicroseconds / 1e6;
  final (lo, hi) = faixaDaCurva(curva);
  final folga = math.max(.25, (hi - lo) * .12);
  final v0 = lo - folga, v1 = hi + folga;
  return Offset(
    quadro.left + seg / dur * quadro.width,
    quadro.bottom - (v - v0) / (v1 - v0) * quadro.height,
  );
}

void main() {
  testWidgets('abrir nao cria curva; + Keyframe crava no cabecote e '
      'preserva o mapeamento', (tester) async {
    final (c, _, id) = await _abrir(tester);
    expect(hasTimeRemap(_clipe(c, id)), isFalse);
    await tester.tap(find.byKey(const ValueKey('estudio-tempo-keyframe')));
    await tester.pump();
    final l = _clipe(c, id);
    final track = timeRemapTrackOf(l)!;
    expect(
      track.keyframes.map((k) => k.time),
      contains(const Duration(seconds: 1)),
    );
    // Velocidade era 2x: o instante mostrado nao muda ao ligar a curva.
    expect(
      videoSourceTimeAt(l, const Duration(seconds: 1)),
      const Duration(seconds: 2),
    );
    expect(
      videoSourceTimeAt(l, const Duration(seconds: 4)),
      const Duration(seconds: 8),
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('abre com os graficos de valor e velocidade do Time Remap', (
    tester,
  ) async {
    await _abrir(tester);
    expect(find.text('Editor de curva · Time Remap'), findsOneWidget);
    expect(
      find.byKey(const ValueKey('estudio-tempo-aba-valor')),
      findsOneWidget,
    );
    expect(
      find.byKey(const ValueKey('estudio-tempo-aba-velocidade')),
      findsOneWidget,
    );
    expect(find.byKey(const ValueKey('estudio-tempo-grafico')), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('tocar na linha cria ponto sem mudar o desenho; arrastar '
      'muda num undo so', (tester) async {
    final (c, _, id) = await _abrir(tester);
    await tester.tap(find.byKey(const ValueKey('estudio-tempo-keyframe')));
    await tester.pump();
    var l = _clipe(c, id);
    var curva = timeRemapTrackOf(l)!;
    final antes = curva.keyframes.length;
    final vAli = valorDaCurva(curva, const Duration(seconds: 2));
    await tester.tapAt(_noGrafico(tester, l, curva, 2, vAli));
    // O grafico tambem escuta toque duplo: o toque simples espera o prazo.
    await tester.pump(const Duration(milliseconds: 400));
    l = _clipe(c, id);
    curva = timeRemapTrackOf(l)!;
    expect(curva.keyframes.length, antes + 1);
    expect(
      valorDaCurva(curva, const Duration(seconds: 2)),
      closeTo(vAli, 1e-6),
      reason: 'criar na linha nao entorta a curva',
    );

    // ARRASTA o ponto novo para cima: o valor muda e UM undo desfaz tudo.
    await tester.dragFrom(
      _noGrafico(tester, l, curva, 2, vAli),
      const Offset(0, -60),
    );
    await tester.pump();
    l = _clipe(c, id);
    final depois = valorDaCurva(
      timeRemapTrackOf(l)!,
      const Duration(seconds: 2),
    );
    expect(depois, greaterThan(vAli + .1));
    c.read(editorControllerProvider.notifier).undo();
    l = _clipe(c, id);
    expect(
      valorDaCurva(timeRemapTrackOf(l)!, const Duration(seconds: 2)),
      closeTo(vAli, 1e-6),
    );
    // O coalesce de undo arma um timer curto; deixa ele vencer.
    await tester.pump(const Duration(seconds: 1));
    expect(tester.takeException(), isNull);
  });

  testWidgets('Congelar estica o clipe e segura o quadro por um segundo', (
    tester,
  ) async {
    final (c, _, id) = await _abrir(tester);
    await tester.tap(find.byKey(const ValueKey('estudio-tempo-congelar')));
    await tester.pump();
    final l = _clipe(c, id);
    expect(l.duration, const Duration(seconds: 5));
    final parado = videoSourceTimeAt(l, const Duration(seconds: 1));
    expect(videoSourceTimeAt(l, const Duration(milliseconds: 1500)), parado);
    expect(
      videoSourceTimeAt(l, const Duration(milliseconds: 2500)),
      isNot(parado),
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('Reverso espelha a curva inteira', (tester) async {
    final (c, _, id) = await _abrir(tester);
    await tester.tap(find.byKey(const ValueKey('estudio-tempo-reverso')));
    await tester.pump();
    final l = _clipe(c, id);
    expect(l.reverse, isFalse);
    expect(videoSourceTimeAt(l, Duration.zero), const Duration(seconds: 8));
    expect(videoSourceTimeAt(l, const Duration(seconds: 4)), Duration.zero);
    expect(tester.takeException(), isNull);
  });

  testWidgets('clipe com Reverso LIGADO assa a curva tocada na primeira '
      'edicao', (tester) async {
    final (c, _, id) = await _abrir(tester, reverso: true);
    final antes = videoSourceTimeAt(_clipe(c, id), const Duration(seconds: 1));
    await tester.tap(find.byKey(const ValueKey('estudio-tempo-keyframe')));
    await tester.pump();
    final l = _clipe(c, id);
    expect(l.reverse, isFalse, reason: 'o interruptor virou curva');
    expect(videoSourceTimeAt(l, const Duration(seconds: 1)), antes);
    expect(videoSourceTimeAt(l, Duration.zero), const Duration(seconds: 4));
    expect(tester.takeException(), isNull);
  });

  testWidgets('aba Velocidade desenha, e o modo IA fica no clipe', (
    tester,
  ) async {
    final (c, _, id) = await _abrir(tester);
    await tester.tap(
      find.byKey(const ValueKey('estudio-tempo-aba-velocidade')),
    );
    await tester.pump();
    await tester.ensureVisible(
      find.byKey(const ValueKey('estudio-tempo-interp-ia')),
    );
    await tester.tap(find.byKey(const ValueKey('estudio-tempo-interp-ia')));
    await tester.pump();
    expect(_clipe(c, id).interpolacao, InterpolacaoDeQuadros.ia);
    expect(tester.takeException(), isNull);
  });

  testWidgets('preset de rampa aplica keyframes num undo so', (tester) async {
    final (c, _, id) = await _abrir(tester);
    await tester.ensureVisible(
      find.byKey(const ValueKey('estudio-tempo-preset-impacto')),
    );
    await tester.tap(
      find.byKey(const ValueKey('estudio-tempo-preset-impacto')),
    );
    await tester.pump();
    expect(
      timeRemapTrackOf(_clipe(c, id))!.keyframes.length,
      greaterThanOrEqualTo(4),
    );
    c.read(editorControllerProvider.notifier).undo();
    expect(hasTimeRemap(_clipe(c, id)), isFalse);
    expect(tester.takeException(), isNull);
  });

  testWidgets('segurar um ponto abre o menu e Suavizar zera a velocidade', (
    tester,
  ) async {
    final (c, _, id) = await _abrir(tester);
    await tester.tap(find.byKey(const ValueKey('estudio-tempo-keyframe')));
    await tester.pump();
    var l = _clipe(c, id);
    final curva = timeRemapTrackOf(l)!;
    await tester.longPressAt(
      _noGrafico(
        tester,
        l,
        curva,
        1,
        valorDaCurva(curva, const Duration(seconds: 1)),
      ),
    );
    await tester.pumpAndSettle();
    await tester.tap(find.text('Suavizar (easy ease)'));
    await tester.pumpAndSettle();
    l = _clipe(c, id);
    final pontos = pontosDaTrilha(timeRemapTrackOf(l)!);
    final ponto = pontos.firstWhere(
      (p) =>
          (p.tempo - const Duration(seconds: 1)).abs() <
          const Duration(milliseconds: 20),
    );
    expect(ponto.entrada!.velocidade, closeTo(0, 1e-6));
    expect(ponto.saida!.velocidade, closeTo(0, 1e-6));
    expect(tester.takeException(), isNull);
  });
}
