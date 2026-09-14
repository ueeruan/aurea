// SEM AVISO DE "EXCLUIDA" (relato dos testadores do beta 1.0.5).
//
// O aviso "Camada excluida / Desfazer" cobria a timeline logo depois de
// cada exclusao, e os testadores pediram para tirar. A camada sumindo ja
// diz o que aconteceu, e o Desfazer continua na barra de reproducao.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/am_timeline.dart'
    show magneticProvider;
import 'package:aurea/src/features/editor/presentation/shell/layer_actions.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

ShapeLayer _retangulo(String id, Duration inicio) => ShapeLayer(
  id: id,
  name: id,
  startTime: inicio,
  duration: const Duration(seconds: 2),
  position: AnimatedOffset(const Offset(10, 10)),
  contents: [
    ShapePath(primitive: ShapePrimitive.rectangle, width: 20, height: 20),
    ShapeFill(color: const Color(0xFFFF0000)),
  ],
);

void main() {
  for (final magnetico in [false, true]) {
    testWidgets('excluir camada nao mostra aviso (magnetico: $magnetico)', (
      tester,
    ) async {
      final container = ProviderContainer(
        overrides: [magneticProvider.overrideWith((ref) => magnetico)],
      );
      addTearDown(container.dispose);
      final editor = container.read(editorControllerProvider.notifier);
      editor.openProject(
        VideoProject(
          name: 'excluir',
          createdAt: DateTime(2026, 9, 14),
          aspectRatio: 1,
          resolutionHeight: 128,
          fps: 30,
          backgroundColor: const Color(0xFF000000),
          layers: [
            _retangulo('a', Duration.zero),
            _retangulo('b', const Duration(seconds: 2)),
          ],
        ),
      );
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: MaterialApp(
            home: Scaffold(
              body: Consumer(
                builder: (context, ref, _) => TextButton(
                  onPressed: () => excluirCamadas(context, ref, {'a'}),
                  child: const Text('excluir'),
                ),
              ),
            ),
          ),
        ),
      );

      await tester.tap(find.text('excluir'));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 600));

      expect(find.byType(SnackBar), findsNothing);
      expect(
        container.read(editorControllerProvider).layers.map((l) => l.id),
        ['b'],
      );
      editor.undo();
      expect(
        container.read(editorControllerProvider).layers.map((l) => l.id),
        unorderedEquals(['a', 'b']),
        reason: 'o Desfazer da barra continua trazendo a camada de volta',
      );
    });
  }
}
