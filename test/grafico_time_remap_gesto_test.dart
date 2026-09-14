// O GRAFICO DO TIME REMAP COM O DEDO, dentro de uma folha modal de verdade.
//
// O defeito que motivou a reescrita so aparecia assim: a folha arrastavel
// ganhava o arrasto vertical e o ponto nao subia. Por isso o editor e
// aberto por `showTimeRemapCurveSheet`, numa tela de celular (390x844), e
// os gestos passam pela arena de verdade.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/cut_ops.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/time_remap_curve_editor.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Cena {
  _Cena(this.container, this.id);

  final ProviderContainer container;
  final String id;
  int vibracoes = 0;

  EditorController get editor =>
      container.read(editorControllerProvider.notifier);

  VideoLayer get video =>
      container.read(editorControllerProvider).layerById(id)! as VideoLayer;

  AnimatedDouble? get trilha => timeRemapTrackOf(video);
}

Future<_Cena> _abrir(WidgetTester tester) async {
  tester.view.physicalSize = const Size(390, 844);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);

  final clipe = VideoLayer(
    name: 'v',
    startTime: Duration.zero,
    duration: const Duration(seconds: 4),
    sourceDuration: const Duration(seconds: 8),
    sourcePath: '/x.mp4',
    position: AnimatedOffset(const Offset(960, 540)),
  );
  final container = ProviderContainer();
  addTearDown(container.dispose);
  container
      .read(editorControllerProvider.notifier)
      .openProject(
        VideoProject(
          name: 'p',
          createdAt: DateTime(2026, 9, 14),
          layers: [clipe],
        ),
      );
  final cena = _Cena(container, clipe.id);

  // Vibracao: conta, e nao deixa a chamada de plataforma sem resposta.
  final mensageiro = tester.binding.defaultBinaryMessenger;
  mensageiro.setMockMethodCallHandler(SystemChannels.platform, (chamada) async {
    if (chamada.method == 'HapticFeedback.vibrate') cena.vibracoes++;
    return null;
  });
  addTearDown(
    () => mensageiro.setMockMethodCallHandler(SystemChannels.platform, null),
  );

  final playback = PlaybackController(
    vsync: tester,
    durationOf: () => const Duration(seconds: 4),
  );
  addTearDown(playback.dispose);

  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        home: Scaffold(
          body: Consumer(
            builder: (context, ref, _) => Align(
              alignment: Alignment.topCenter,
              child: TextButton(
                onPressed: () =>
                    showTimeRemapCurveSheet(context, ref, clipe.id, playback),
                child: const Text('abrir'),
              ),
            ),
          ),
        ),
      ),
    ),
  );
  await tester.tap(find.text('abrir'));
  await tester.pumpAndSettle();
  expect(find.byType(TimeRemapCurveEditor), findsOneWidget);
  return cena;
}

Offset _ponto(WidgetTester tester, int i) =>
    tester.getCenter(find.byKey(ValueKey('curva-ponto-$i')));

/// Sobe o ultimo ponto [passos] x 12 px, como um dedo.
Future<void> _subirUltimoPonto(WidgetTester tester, {int passos = 6}) async {
  final gesto = await tester.startGesture(_ponto(tester, 1));
  await tester.pump();
  for (var k = 0; k < passos; k++) {
    await gesto.moveBy(const Offset(0, -12));
    await tester.pump();
  }
  await gesto.up();
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('arrasto vertical num ponto muda o valor e a folha nao fecha', (
    tester,
  ) async {
    final cena = await _abrir(tester);
    expect(cena.trilha, isNull, reason: 'abrir o grafico nao cria remap');

    await _subirUltimoPonto(tester);

    final trilha = cena.trilha;
    expect(trilha, isNotNull, reason: 'a primeira edicao cria o remap');
    expect(trilha!.keyframes, hasLength(2));
    expect(
      trilha.keyframes.last.time,
      const Duration(seconds: 4),
      reason: 'a ponta so anda na vertical',
    );
    expect(trilha.keyframes.last.value, greaterThan(4.5));
    expect(
      find.byType(TimeRemapCurveEditor),
      findsOneWidget,
      reason: 'o arrasto vertical e do grafico, nao da folha',
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('tocar na linha cria um ponto ali', (tester) async {
    final cena = await _abrir(tester);
    final meio = Offset.lerp(_ponto(tester, 0), _ponto(tester, 1), 0.5)!;

    await tester.tapAt(meio);
    await tester.pump();

    final trilha = cena.trilha!;
    expect(trilha.keyframes, hasLength(3));
    expect(trilha.keyframes[1].time, const Duration(seconds: 2));
    expect(trilha.keyframes[1].value, closeTo(2, 1e-9));
    expect(
      videoSourceTimeAt(cena.video, const Duration(seconds: 3)),
      const Duration(seconds: 3),
      reason: 'criar o ponto nao muda nenhum quadro',
    );
    expect(find.byKey(const ValueKey('curva-ponto-2')), findsOneWidget);
    expect(cena.vibracoes, greaterThan(0));
    expect(tester.takeException(), isNull);
  });

  testWidgets('um desfazer reverte o arrasto inteiro', (tester) async {
    final cena = await _abrir(tester);
    final gesto = await tester.startGesture(_ponto(tester, 1));
    await tester.pump();
    await gesto.moveBy(const Offset(0, -20));
    await tester.pump();
    // Uma pausa de verdade no meio do arrasto: fora de um gesto, a janela
    // de 450 ms do controlador partiria isto em dois passos de desfazer.
    await tester.runAsync(
      () => Future<void>.delayed(const Duration(milliseconds: 520)),
    );
    await gesto.moveBy(const Offset(0, -20));
    await tester.pump();
    await gesto.moveBy(const Offset(0, -20));
    await tester.pump();
    await gesto.up();
    await tester.pumpAndSettle();
    expect(cena.trilha, isNotNull);

    cena.editor.undo();
    await tester.pump();

    expect(cena.trilha, isNull, reason: 'volta ao clipe sem remap');
    expect(
      cena.editor.canUndo,
      isFalse,
      reason: 'o arrasto inteiro era um passo so',
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('desfazer atualiza o grafico', (tester) async {
    final cena = await _abrir(tester);
    await tester.tapAt(Offset.lerp(_ponto(tester, 0), _ponto(tester, 1), 0.5)!);
    await tester.pump();
    expect(find.byKey(const ValueKey('curva-ponto-2')), findsOneWidget);

    cena.editor.undo();
    await tester.pump();

    expect(find.byKey(const ValueKey('curva-ponto-2')), findsNothing);
    expect(find.byKey(const ValueKey('curva-ponto-1')), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('"Reto (1x)" volta a reta', (tester) async {
    final cena = await _abrir(tester);
    await _subirUltimoPonto(tester);
    expect(cena.trilha!.keyframes.last.value, greaterThan(4.5));

    final reto = find.byKey(const ValueKey('curva-pronto-reto'));
    await tester.ensureVisible(reto);
    await tester.pumpAndSettle();
    await tester.tap(reto);
    await tester.pump();

    final trilha = cena.trilha!;
    expect(trilha.keyframes.map((k) => k.time), [
      Duration.zero,
      const Duration(seconds: 4),
    ]);
    expect(trilha.keyframes.map((k) => k.value), [0, 4]);
    expect(
      videoSourceTimeAt(cena.video, const Duration(seconds: 1)),
      const Duration(seconds: 1),
    );
    expect(tester.takeException(), isNull);
  });
}
