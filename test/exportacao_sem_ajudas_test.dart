import 'dart:ui' as ui;

import 'package:aurea_render/aurea_render.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/null_gizmo_painter.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// O QUE E AJUDA NAO SAI NO VIDEO.
///
/// Relato do beta, e grave: "no exportar ta aparecendo os negocios do
/// nulo". O objeto nulo desenha um quadrado tracejado com um X para se
/// ver o que se esta arrastando — uma ajuda de edicao. O comentario no
/// codigo dizia "so no editor", mas o desenho nao perguntava se estava
/// exportando: o gizmo ia junto no arquivo entregue.
///
/// Este teste olha o PIXEL. Uma cena com um nulo e uma camada de
/// particulas, exportada, nao pode ter nenhum traco do roxo do gizmo;
/// no editor, ela tem.
VideoProject _projeto() {
  final nulo = NullLayer(
    id: 'nulo',
    name: 'Nulo',
    startTime: Duration.zero,
    duration: const Duration(seconds: 5),
    position: AnimatedOffset(const Offset(150, 150)),
  );
  final particulas = ParticulasLayer(
    id: 'p',
    name: 'Particulas',
    startTime: Duration.zero,
    duration: const Duration(seconds: 5),
    parametros: ParametrosDeParticulas(maximo: 12, vidaS: 4),
    position: AnimatedOffset(const Offset(150, 150)),
  );
  return VideoProject(
    name: 'export',
    createdAt: DateTime(2026, 9, 8),
    aspectRatio: 1,
    resolutionHeight: 300,
    backgroundColor: const Color(0xFF000000),
    layers: [particulas, nulo],
  );
}

/// Quantos pixels tem a cor do gizmo do nulo (roxo 0xFF9F8CFF, meio
/// transparente sobre o preto).
Future<int> _pixelsDoGizmo(WidgetTester tester, {required bool exporting}) async {
  final container = ProviderContainer();
  addTearDown(container.dispose);
  container.read(editorControllerProvider.notifier).openProject(_projeto());
  final time = ValueNotifier(const Duration(seconds: 1));
  addTearDown(time.dispose);
  final videos = VideoLayerManager();
  addTearDown(videos.dispose);
  final chave = GlobalKey();

  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        home: Center(
          child: RepaintBoundary(
            key: chave,
            child: SizedBox(
              width: 300,
              height: 300,
              child: ColoredBox(
                color: const Color(0xFF000000),
                child: CompositionView(
                  time: time,
                  videos: videos,
                  selectedId: null,
                  exporting: exporting,
                ),
              ),
            ),
          ),
        ),
      ),
    ),
  );
  await tester.pumpAndSettle();

  final imagem = (await tester.runAsync(
    () => (chave.currentContext!.findRenderObject() as RenderRepaintBoundary)
        .toImage(),
  ))!;
  final bytes = (await tester.runAsync(
    () => imagem.toByteData(format: ui.ImageByteFormat.rawRgba),
  ))!;
  var n = 0;
  for (var i = 0; i < bytes.lengthInBytes; i += 4) {
    final r = bytes.getUint8(i);
    final g = bytes.getUint8(i + 1);
    final b = bytes.getUint8(i + 2);
    // O roxo do gizmo: muito azul, verde no meio, e azul acima do
    // vermelho. As particulas do teste sao brancas.
    if (b > 90 && b > r + 20 && r > g + 8 && r < 220) n++;
  }
  imagem.dispose();
  return n;
}

void main() {
  testWidgets('exportando, o gizmo do nulo nao pinta um pixel', (tester) async {
    final n = await _pixelsDoGizmo(tester, exporting: true);
    expect(n, 0, reason: 'ajuda de edicao dentro do video entregue');
    expect(tester.takeException(), isNull);
  });

  testWidgets('no editor, o gizmo do nulo continua a vista', (tester) async {
    final n = await _pixelsDoGizmo(tester, exporting: false);
    expect(
      n,
      greaterThan(200),
      reason: 'sem o quadrado tracejado nao se ve o que se arrasta',
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('nada de ajuda no widget exportado: nem o pintor entra na arvore', (
    tester,
  ) async {
    for (final exportando in [true, false]) {
      final container = ProviderContainer();
      addTearDown(container.dispose);
      container.read(editorControllerProvider.notifier).openProject(_projeto());
      final time = ValueNotifier(const Duration(seconds: 1));
      addTearDown(time.dispose);
      final videos = VideoLayerManager();
      addTearDown(videos.dispose);
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: MaterialApp(
            home: SizedBox(
              width: 300,
              height: 300,
              child: CompositionView(
                time: time,
                videos: videos,
                selectedId: null,
                exporting: exportando,
              ),
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();
      final gizmos = find
          .byWidgetPredicate((w) => w is CustomPaint && w.painter is NullGizmoPainter)
          .evaluate()
          .length;
      expect(
        gizmos,
        exportando ? 0 : 1,
        reason: exportando ? 'o gizmo foi montado na exportacao' : 'sumiu do editor',
      );
    }
  });
}
