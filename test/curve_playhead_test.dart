import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/curve_panel.dart';

/// Um TickerProvider avulso para o relogio do teste.
class _Vsync extends TickerProvider {
  @override
  Ticker createTicker(TickerCallback onTick) => Ticker(onTick);
}

void main() {
  testWidgets('o painel de curva nunca move o cabecote', (tester) async {
    // POR QUE ESTE TESTE EXISTE: o painel puxava o cabecote de volta para
    // dentro do trecho de keyframes sempre que ele saia — e fazia isso
    // DENTRO do build. Com a reproducao andando virava um cabo de guerra:
    // o relogio avancava, o painel puxava, o relogio avancava de novo. O
    // cabecote ia e voltava e nao dava para animar.
    final playback = PlaybackController(
      vsync: _Vsync(),
      durationOf: () => const Duration(seconds: 10),
    );
    addTearDown(playback.dispose);

    // Camada com dois keyframes de posicao entre 0 e 1s — ou seja, o
    // trecho editavel cobre so o primeiro segundo.
    final camada = ShapeLayer(
      name: 'Forma',
      startTime: Duration.zero,
      duration: const Duration(seconds: 10),
      position: AnimatedOffset(Offset.zero, [
        const Keyframe(time: Duration.zero, value: Offset.zero),
        const Keyframe(
            time: Duration(seconds: 1), value: Offset(400, 300)),
      ]),
    );

    final projeto = VideoProject(
        name: 'teste', createdAt: DateTime(2026), layers: [camada]);

    final container = ProviderContainer();
    addTearDown(container.dispose);
    container.read(editorControllerProvider.notifier).openProject(projeto);
    container.read(selectedLayerProvider.notifier).state = camada.id;

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: Scaffold(
            body: SizedBox(
              height: 300,
              child: CurvePanel(
                playback: playback,
                prop: LayerProp.position,
                onBack: () {},
              ),
            ),
          ),
        ),
      ),
    );

    // FORA do trecho: e exatamente aqui que o painel puxava.
    playback.seek(const Duration(seconds: 4));
    final parado = playback.time.value;
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 32));
    expect(playback.time.value, parado,
        reason: 'o painel deslocou o cabecote que estava fora do trecho');

    // E com o relogio ANDANDO o avanco tem de ser monotono.
    playback.play();
    var anterior = playback.time.value;
    for (var i = 0; i < 40; i++) {
      await tester.pump(const Duration(milliseconds: 33));
      final agora = playback.time.value;
      expect(agora >= anterior, isTrue,
          reason: 'o cabecote retrocedeu de $anterior para $agora');
      anterior = agora;
    }
    expect(anterior > parado, isTrue,
        reason: 'o cabecote nao avancou durante a reproducao');
    playback.pause();
  });
}
