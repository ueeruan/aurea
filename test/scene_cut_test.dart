import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/scene_cut_service.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

/// Decupar sozinho: os cortes de cena que o FFmpeg imprime viram pedacos
/// na linha do tempo — um passo so de desfazer — ou marcas na regua.
void main() {
  group('parseSceneCuts', () {
    const log = '''
[Parsed_showinfo_2 @ 0x1] n:   0 pts:      0 pts_time:0        duration:...
[Parsed_showinfo_2 @ 0x1] n:   1 pts:  75075 pts_time:2.5025   duration:...
[Parsed_showinfo_2 @ 0x1] n:   2 pts:  78078 pts_time:2.6026   duration:...
[Parsed_showinfo_2 @ 0x1] n:   3 pts: 300300 pts_time:10.01    duration:...
''';

    test('le os pts_time, ignora o primeiro quadro e funde vizinhos', () {
      final cortes = parseSceneCuts(log);
      expect(cortes.length, 2);
      expect(cortes[0].inMilliseconds, closeTo(2502, 1));
      expect(cortes[1].inMilliseconds, closeTo(10010, 1));
    });

    test('sem showinfo, sem cortes', () {
      expect(parseSceneCuts('frame=  120 fps=0.0 q=-0.0'), isEmpty);
    });
  });

  group('splitLayerAtTimes', () {
    VideoLayer clipe() => VideoLayer(
          id: 'v',
          name: 'v',
          startTime: Duration.zero,
          duration: const Duration(seconds: 10),
          sourcePath: '/x.mp4',
        );

    ProviderContainer preparar() {
      final projeto = VideoProject(
        name: 'p',
        createdAt: DateTime(2026),
        layers: [clipe()],
      );
      final c = ProviderContainer();
      c.read(editorControllerProvider.notifier).openProject(projeto);
      return c;
    }

    test('tres cortes viram quatro pedacos contiguos, fonte seguida', () {
      final c = preparar();
      final ctrl = c.read(editorControllerProvider.notifier);
      final ids = ctrl.splitLayerAtTimes('v', const [
        Duration(seconds: 7),
        Duration(seconds: 2),
        Duration(seconds: 4),
      ]);
      expect(ids.length, 4);
      final pedacos = c
          .read(editorControllerProvider)
          .layers
          .whereType<VideoLayer>()
          .toList()
        ..sort((a, b) => a.startTime.compareTo(b.startTime));
      expect(pedacos.length, 4);
      expect(pedacos.map((l) => l.startTime.inSeconds).toList(), [0, 2, 4, 7]);
      expect(pedacos.map((l) => l.duration.inSeconds).toList(), [2, 2, 3, 3]);
      // A fonte continua: cada pedaco comeca onde o anterior parou.
      expect(
          pedacos.map((l) => l.sourceOffset.inSeconds).toList(), [0, 2, 4, 7]);
    });

    test('corte fora do clipe ou em cima da ponta e ignorado', () {
      final c = preparar();
      final ctrl = c.read(editorControllerProvider.notifier);
      final ids = ctrl.splitLayerAtTimes('v', const [
        Duration(seconds: 15),
        Duration(milliseconds: 20),
        Duration(seconds: 5),
      ]);
      expect(ids.length, 2);
      expect(c.read(editorControllerProvider).layers.length, 2);
    });

    test('um desfazer so volta o clipe inteiro', () {
      final c = preparar();
      final ctrl = c.read(editorControllerProvider.notifier);
      ctrl.splitLayerAtTimes(
          'v', const [Duration(seconds: 3), Duration(seconds: 6)]);
      expect(c.read(editorControllerProvider).layers.length, 3);
      ctrl.undo();
      expect(c.read(editorControllerProvider).layers.length, 1);
    });
  });

  test('addMarkers nao duplica marca que ja existe perto', () {
    final projeto = VideoProject(
      name: 'p',
      createdAt: DateTime(2026),
      layers: [],
    );
    final c = ProviderContainer();
    final ctrl = c.read(editorControllerProvider.notifier);
    ctrl.openProject(projeto);
    ctrl.addMarkers(const [Duration(seconds: 1), Duration(seconds: 2)]);
    ctrl.addMarkers(const [
      Duration(milliseconds: 1050),
      Duration(seconds: 3),
      Duration(milliseconds: 3040),
    ]);
    final marcas = c.read(editorControllerProvider).markers;
    expect(marcas.map((m) => m.time.inMilliseconds).toList(), [1000, 2000, 3000]);
  });
}
