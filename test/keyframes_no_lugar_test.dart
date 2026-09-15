// "OS KEYFRAMES NAO FICAM CERTOS" (print do beta, 15/09): aparar a alca
// ESQUERDA de video/audio nao rebaseava a animacao — so as camadas sem
// midia deslocavam os keyframes junto com a origem do tempo local. O
// keyframe feito no quadro X tem de continuar disparando no quadro X.
//
// E o nome-hash: galeria entrega "3d907d6ca2ca..." como titulo, a pilula
// compacta mostrava o hash e a camada parecia "mudar de formato do nada".
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/cut_ops.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  (ProviderContainer, EditorController) montar(List<Layer> layers) {
    final c = ProviderContainer();
    final e = c.read(editorControllerProvider.notifier);
    e.openProject(
      VideoProject(name: 'p', createdAt: DateTime(2026), layers: layers),
    );
    return (c, e);
  }

  group('aparar o inicio leva os keyframes junto', () {
    test('video comum: o keyframe continua no MESMO instante do filme', () {
      final v = VideoLayer(
        name: 'v',
        startTime: const Duration(seconds: 2),
        duration: const Duration(seconds: 6),
        sourcePath: 'x.mp4',
        sourceDuration: const Duration(seconds: 30),
        opacity: AnimatedDouble(
          1,
        ).withKeyframe(const Duration(seconds: 2), 0.25),
      );
      final (c, e) = montar([v]);
      addTearDown(c.dispose);

      // O keyframe vive no local 2s = global 4s.
      e.trimLayerStart(v.id, const Duration(seconds: 3));
      final depois =
          c.read(editorControllerProvider).layerById(v.id)! as VideoLayer;
      expect(depois.startTime, const Duration(seconds: 3));
      // Rebaseado: local 1s — o MESMO global 4s de antes.
      expect(depois.opacity.keyframes.single.time, const Duration(seconds: 1));
      expect(depois.opacity.keyframes.single.value, 0.25);
      // E o filme tambem andou (in-point da fonte).
      expect(depois.sourceOffset, const Duration(seconds: 1));
    });

    test('audio: mesma regra', () {
      final a = AudioLayer(
        name: 'a',
        startTime: Duration.zero,
        duration: const Duration(seconds: 8),
        sourcePath: 'x.m4a',
        opacity: AnimatedDouble(
          1,
        ).withKeyframe(const Duration(seconds: 4), 0.5),
      );
      final (c, e) = montar([a]);
      addTearDown(c.dispose);
      e.trimLayerStart(a.id, const Duration(seconds: 1));
      final depois =
          c.read(editorControllerProvider).layerById(a.id)! as AudioLayer;
      expect(depois.opacity.keyframes.single.time, const Duration(seconds: 3));
    });

    test('video em reverso (vira trilha de tempo): keyframes rebaseiam', () {
      final v = VideoLayer(
        name: 'v',
        startTime: Duration.zero,
        duration: const Duration(seconds: 8),
        sourcePath: 'x.mp4',
        sourceDuration: const Duration(seconds: 12),
        reverse: true,
        opacity: AnimatedDouble(
          1,
        ).withKeyframe(const Duration(seconds: 4), 0.5),
      );
      final (c, e) = montar([v]);
      addTearDown(c.dispose);
      e.trimLayerStart(v.id, const Duration(seconds: 2));
      final depois =
          c.read(editorControllerProvider).layerById(v.id)! as VideoLayer;
      expect(depois.reverse, isFalse);
      expect(hasTimeRemap(depois), isTrue);
      expect(depois.opacity.keyframes.single.time, const Duration(seconds: 2));
    });

    test('a camada sem midia continua rebaseando (paridade)', () {
      final t = TextLayer(
        name: 't',
        text: 'oi',
        startTime: Duration.zero,
        duration: const Duration(seconds: 6),
        opacity: AnimatedDouble(
          1,
        ).withKeyframe(const Duration(seconds: 3), 0.5),
      );
      final (c, e) = montar([t]);
      addTearDown(c.dispose);
      e.trimLayerStart(t.id, const Duration(seconds: 1));
      final depois = c.read(editorControllerProvider).layerById(t.id)!;
      expect(depois.opacity.keyframes.single.time, const Duration(seconds: 2));
    });
  });

  group('batismo de midia importada', () {
    test('nome-hash vira "Vídeo 1"; nome de gente so perde a extensao', () {
      final (c, e) = montar(const []);
      addTearDown(c.dispose);
      final id1 = e.addVideoLayer(
        Duration.zero,
        '/g/3d907d6ca2ca738267c4a7.mp4',
        '3d907d6ca2ca738267c4a7',
        const Duration(seconds: 4),
      );
      expect(c.read(editorControllerProvider).layerById(id1)!.name, 'Vídeo 1');

      final id2 = e.addVideoLayer(
        Duration.zero,
        '/g/praia.mp4',
        'praia.mp4',
        const Duration(seconds: 4),
      );
      expect(c.read(editorControllerProvider).layerById(id2)!.name, 'praia');
    });

    test('numerao de galeria vira "Imagem 1"; nome com letras fica', () {
      final (c, e) = montar(const []);
      addTearDown(c.dispose);
      final id1 = e.addImageLayer(
        Duration.zero,
        '/g/1000012345.jpg',
        '1000012345.jpg',
      );
      expect(c.read(editorControllerProvider).layerById(id1)!.name, 'Imagem 1');

      final id2 = e.addImageLayer(
        Duration.zero,
        '/g/IMG-2026 casa.jpg',
        'IMG-2026 casa.jpg',
      );
      expect(
        c.read(editorControllerProvider).layerById(id2)!.name,
        'IMG-2026 casa',
      );
    });
  });
}
