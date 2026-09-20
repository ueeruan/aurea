// KEYFRAME AUTOMATICO LIGADO A MAO.
//
// O automatico NASCE DESLIGADO (`docs/keyframe-explicito.md`): editar um
// valor nunca crava marca sozinho. Quem liga o interruptor de proposito
// passa a cravar — mas so no instante editado, e so em trilha que JA
// esta animada. A ANCORA saiu junto com o padrao antigo: ela cravava uma
// SEGUNDA marca em tempo zero, num instante que a pessoa nunca visitou.
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';

void main() {
  test('nasce desligado: nem com o interruptor a trilha estatica ganha marca', () {
    final ref = ProviderContainer();
    addTearDown(ref.dispose);
    expect(
      ref.read(autoKeyframeProvider),
      isFalse,
      reason: 'keyframe nao nasce sem intencao',
    );
    final editor = ref.read(editorControllerProvider.notifier);
    editor.openProject(
      ref.read(editorControllerProvider).copyWith(
        layers: [
          ShapeLayer(
            id: 'shape',
            name: 'Motion',
            startTime: const Duration(seconds: 3),
            duration: const Duration(seconds: 5),
            position: AnimatedOffset(const Offset(10, 20)),
          ),
        ],
      ),
    );
    ref.read(autoKeyframeProvider.notifier).state = true;
    editor.editPosition('shape', const Duration(seconds: 5), const Offset(50, 60));
    final layer = ref.read(editorControllerProvider).layerById('shape')!;
    expect(layer.position.isAnimated, isFalse);
    expect(layer.position.valueAt(Duration.zero), const Offset(50, 60));
  });

  test('ligado: crava no tempo LOCAL da camada, e so ali', () {
    final ref = ProviderContainer();
    addTearDown(ref.dispose);
    final editor = ref.read(editorControllerProvider.notifier);
    editor.openProject(
      ref.read(editorControllerProvider).copyWith(
        layers: [
          ShapeLayer(
            id: 'shape',
            name: 'Motion',
            startTime: const Duration(seconds: 3),
            duration: const Duration(seconds: 5),
            // JA ANIMADA: a marca de zero e da pessoa, nao da ancora.
            position: AnimatedOffset(
              const Offset(10, 20),
            ).withKeyframe(Duration.zero, const Offset(10, 20)),
          ),
        ],
      ),
    );
    ref.read(autoKeyframeProvider.notifier).state = true;
    editor.editPosition('shape', const Duration(seconds: 5), const Offset(50, 60));
    final layer = ref.read(editorControllerProvider).layerById('shape')!;
    expect(layer.startTime, const Duration(seconds: 3));
    expect(layer.position.valueAt(Duration.zero), const Offset(10, 20));
    expect(
      layer.position.valueAt(const Duration(seconds: 2)),
      const Offset(50, 60),
      reason: 'o tempo global 5 s e o local 2 s',
    );
    expect(layer.position.keyframes.length, 2);
    editor.undo();
    expect(
      ref
          .read(editorControllerProvider)
          .layerById('shape')!
          .position
          .keyframes
          .length,
      1,
    );
  });

  test('ligado: a rotacao grava os tres eixos', () {
    final ref = ProviderContainer();
    addTearDown(ref.dispose);
    final editor = ref.read(editorControllerProvider.notifier);
    editor.addShapeLayer(Duration.zero);
    final id = ref.read(editorControllerProvider).layers.first.id;
    const time = Duration(seconds: 1);
    // A trilha entra animada pelo losango — comando explicito.
    editor.toggleKeyframe(id, Duration.zero, LayerProp.rotation);
    ref.read(autoKeyframeProvider.notifier).state = true;
    editor.editRotation(id, time, 90);
    editor.editScaleUniform(id, time, 2);
    final layer = ref.read(editorControllerProvider).layerById(id)!;
    expect(layer.rotation.valueAt(Duration.zero), 0);
    expect(layer.rotation.valueAt(time), 90);
    expect(layer.rotationX.hasKeyframeAt(time), isTrue);
    expect(layer.rotationY.hasKeyframeAt(time), isTrue);
    // A escala continua estatica: sem marca, a edicao muda a base.
    expect(layer.scaleX.valueAt(Duration.zero), 2);
  });
}
