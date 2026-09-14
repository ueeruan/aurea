import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/cut_ops.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/temporal_interpolation.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

void main() {
  test('Time Remap is not addable and Optical Flow survives saving', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final editor = c.read(editorControllerProvider.notifier);
    editor.openProject(
      VideoProject(
        name: 'test',
        createdAt: DateTime(2026),
        layers: [
          VideoLayer(
            id: 'v',
            name: 'video',
            sourcePath: '/video.mp4',
            startTime: const Duration(seconds: 2),
            duration: const Duration(seconds: 4),
            sourceOffset: const Duration(seconds: 1),
            speed: .5,
          ),
        ],
      ),
    );
    VideoLayer read() =>
        c.read(editorControllerProvider).layers.single as VideoLayer;
    final before = videoAbsoluteSourceTimeAt(
      read(),
      const Duration(seconds: 2),
    );
    editor.addEffect('v', EffectType.timeRemap);
    expect(
      videoAbsoluteSourceTimeAt(read(), const Duration(seconds: 2)),
      before,
    );
    expect(timeRemapTrackOf(read()), isNull, reason: 'Time Remap saiu do app');
    editor.addEffect('v', EffectType.opticalFlow);
    expect(fatorDeInterpolacao(read()), 2);
    expect(filtroDeInterpolacao(read(), fps: 30), contains('mi_mode=mci'));
    final saved = projectFromJson(
      projectToJson(c.read(editorControllerProvider)),
    );
    expect(
      interpolacaoEfetiva(saved.layers.single as VideoLayer),
      InterpolacaoDeQuadros.movimento,
    );
    final flow = read().effects.firstWhere(
      (e) => e.type == EffectType.opticalFlow,
    );
    editor.toggleEffectEnabled('v', flow.id);
    expect(filtroDeInterpolacao(read(), fps: 30), '');
    editor.toggleEffectEnabled('v', flow.id);
    expect(fatorDeInterpolacao(read()), 2);
    editor.removeEffect('v', flow.id);
    expect(fatorDeInterpolacao(read()), 1);
  });
}
