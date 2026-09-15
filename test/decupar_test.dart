// A DECUPAGEM: a conta do tempo (fonte -> linha) e as recusas.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/cut_ops.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  VideoLayer clipe({double speed = 1, bool reverse = false}) => VideoLayer(
    name: 'v',
    startTime: const Duration(seconds: 2),
    duration: const Duration(seconds: 4),
    sourceDuration: const Duration(seconds: 30),
    sourcePath: 'x.mp4',
    sourceOffset: const Duration(seconds: 3),
    speed: speed,
    reverse: reverse,
  );

  test('cortes relativos a fonte caem na linha esticados pela velocidade', () {
    final v = clipe(speed: 2);
    final globais = temposGlobaisDosCortes(v, const [
      Duration(seconds: 2),
      Duration(seconds: 4),
    ]);
    // 2 s de fonte a 2x = 1 s de linha, a partir do inicio da camada.
    expect(globais, const [
      Duration(seconds: 3),
      Duration(seconds: 4),
    ]);
  });

  test('corte colado na borda vira farelo e sai da lista', () {
    final v = clipe();
    final globais = temposGlobaisDosCortes(v, const [
      Duration(milliseconds: 50),
      Duration(seconds: 2),
      Duration(milliseconds: 3950),
    ]);
    expect(globais, const [Duration(seconds: 4)]);
  });

  test('reverso e Time Remap recusam antes de ler o video', () async {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    final v = clipe(reverse: true);
    e.openProject(
      VideoProject(name: 'p', createdAt: DateTime(2026), layers: [v]),
    );
    // Nenhum canal de plataforma e tocado: a recusa vem primeiro.
    expect(await e.decuparCamada(v.id), isNull);
    expect(await e.cortesDeCenaViramMarcas(v.id), isNull);

    final comCurva = clipe();
    e.openProject(
      VideoProject(name: 'p2', createdAt: DateTime(2026), layers: [comCurva]),
    );
    e.definirTrilhaDeTempo(
      comCurva.id,
      AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(const Duration(seconds: 4), 1.5),
    );
    expect(await e.decuparCamada(comCurva.id), isNull);
  });
}
