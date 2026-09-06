import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

void main() {
  test('meta so com extrude nao e vazia e sobrevive ao salvar', () {
    expect(const LayerMeta(extrude: 80).isEmpty, isFalse);
    expect(LayerMeta.empty.isEmpty, isTrue);

    final camada = TextLayer(
      name: 't',
      startTime: Duration.zero,
      duration: const Duration(seconds: 1),
      text: 'oi',
      rotationY: AnimatedDouble(40),
      is3D: true,
    );
    final projeto = VideoProject(
      name: 'p',
      createdAt: DateTime(2026, 1, 1),
      layers: [camada],
      meta: {camada.id: const LayerMeta(extrude: 80)},
    );
    final volta = projectFromJson(projectToJson(projeto));
    expect(volta.metaOf(camada.id).extrude, 80);
  });
}
