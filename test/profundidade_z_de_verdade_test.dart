// RELATO 14/09: "a profundidade Z ta com bug, nao funciona como deveria".
// O Z so encolhia a camada plana (ela nao ia para o ponto de fuga, como os
// solidos 3D), a camera da composicao nao via camada 3D sem pai (a moldura
// de selecao via, e ficava em outro lugar), a pintura ordenava pelo Z
// proprio ignorando o nulo pai, e passar da camera travava a camada em 12x.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/selection_geometry.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/composition_frame.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

Finder _conteudoDa(String id) => find.byWidgetPredicate(
  (w) =>
      w.runtimeType.toString() == '_LayerContent' &&
      ((w as dynamic).layer as Layer).id == id,
);

/// Onde a camada foi DESENHADA e onde a selecao diz que ela esta, em
/// coordenadas da composicao.
({Offset desenhado, Offset selecao}) _medir(
  WidgetTester tester,
  ProviderContainer c,
  String id,
) {
  final project = c.read(editorControllerProvider);
  final layer = project.layerById(id)!;
  final stage = tester.getRect(find.byType(PreviewStage));
  final comp = compositionRect(
    stage.size,
    Size(project.outputWidth.toDouble(), project.outputHeight.toDouble()),
  );
  final fator = comp.width / project.outputWidth;
  final caixa = c
      .read(editorControllerProvider.notifier)
      .layerBoxRect(layer, Duration.zero, scaled: false);
  final selecao = MatrixUtils.transformPoint(
    selectionTransform(project, layer, Duration.zero),
    caixa.center,
  );
  final tela = tester.getCenter(_conteudoDa(id));
  return (
    desenhado: (tela - stage.topLeft - comp.topLeft) / fator,
    selecao: selecao,
  );
}

Future<(ProviderContainer, String, Offset)> _soUmaCamada(
  WidgetTester tester,
) async {
  final c = await openEditor(tester);
  final e = c.read(editorControllerProvider.notifier);
  final id = c.read(editorControllerProvider).layers.first.id;
  for (final l in c.read(editorControllerProvider).layers.toList()) {
    if (l.id != id) e.removeLayer(l.id);
  }
  final p = c.read(editorControllerProvider);
  return (c, id, Offset(p.outputWidth / 2, p.outputHeight / 2));
}

void main() {
  test('recuar em Z encolhe E leva a camada ao ponto de fuga', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final p = c.read(editorControllerProvider);
    final centro = Offset(p.outputWidth / 2, p.outputHeight / 2);
    final longe = projetarProfundidade(
      p,
      centro + const Offset(300, -200),
      1200,
    )!;
    expect(longe.escala, closeTo(.5, 1e-9));
    expect(
      (longe.pos - (centro + const Offset(150, -100))).distance,
      lessThan(1e-9),
    );
    final perto = projetarProfundidade(p, centro + const Offset(100, 0), -600)!;
    expect(perto.escala, closeTo(2, 1e-9));
    expect(
      (perto.pos - (centro + const Offset(200, 0))).distance,
      lessThan(1e-9),
    );
    expect(
      projetarProfundidade(p, centro, -1150),
      isNull,
      reason: 'passou da camera',
    );
  });

  test('a pintura segue o Z EFETIVO: filho de nulo que recuou vai para tras', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addTextLayer(Duration.zero, text: 'filho');
    e.addTextLayer(Duration.zero, text: 'solto');
    e.addNullLayer(Duration.zero);
    var p = c.read(editorControllerProvider);
    final filho = p.layers
        .firstWhere((l) => l is TextLayer && l.text == 'filho')
        .id;
    final solto = p.layers
        .firstWhere((l) => l is TextLayer && l.text == 'solto')
        .id;
    final nulo = p.layers.whereType<NullLayer>().first.id;
    e.editPositionZ(filho, Duration.zero, 0);
    e.editPositionZ(solto, Duration.zero, 100);
    e.editPositionZ(nulo, Duration.zero, 0);
    e.linkProperty(filho, LayerProp.parent, nulo, Duration.zero);
    e.editPositionZ(nulo, Duration.zero, 800);
    p = c.read(editorControllerProvider);
    final ordem =
        depthSortPaintOrder(p.layers.reversed.toList(), Duration.zero, project: p)
            .where((l) => l.id == filho || l.id == solto)
            .map((l) => l.id)
            .toList();
    // O filho esta em Z 800 pelo nulo: pinta antes do solto, em Z 100.
    expect(ordem, [filho, solto]);
  });

  testWidgets('camada fora do centro recua para o ponto de fuga no palco', (
    tester,
  ) async {
    final (c, id, centro) = await _soUmaCamada(tester);
    final e = c.read(editorControllerProvider.notifier);
    e.editPosition(id, Duration.zero, centro + const Offset(240, -300));
    e.editPositionZ(id, Duration.zero, 1200);
    await tester.pumpAndSettle();
    final m = _medir(tester, c, id);
    final esperado = centro + const Offset(120, -150);
    expect((m.selecao - esperado).distance, lessThan(.5));
    expect((m.desenhado - esperado).distance, lessThan(1.5));
    expect(tester.takeException(), isNull);
  });

  testWidgets('a camera da composicao move a camada 3D sem pai no palco', (
    tester,
  ) async {
    final (c, id, centro) = await _soUmaCamada(tester);
    final e = c.read(editorControllerProvider.notifier);
    e.editPosition(id, Duration.zero, centro + const Offset(100, 50));
    e.editPositionZ(id, Duration.zero, 0);
    e.addCameraLayer(Duration.zero);
    final cam = c
        .read(editorControllerProvider)
        .layers
        .whereType<CameraLayer>()
        .first
        .id;
    e.editPosition(cam, Duration.zero, centro + const Offset(200, 0));
    await tester.pumpAndSettle();
    final m = _medir(tester, c, id);
    // Camera 200 px a direita: a cena anda 200 px para a esquerda.
    final esperado = centro + const Offset(-100, 50);
    expect((m.selecao - esperado).distance, lessThan(.5));
    expect((m.desenhado - esperado).distance, lessThan(1.5));
    expect(tester.takeException(), isNull);
  });

  testWidgets('camada que passou da camera some e nao se toca', (tester) async {
    final (c, id, centro) = await _soUmaCamada(tester);
    final e = c.read(editorControllerProvider.notifier);
    e.editPositionZ(id, Duration.zero, -1150);
    await tester.pumpAndSettle();
    expect(_conteudoDa(id), findsNothing);
    final p = c.read(editorControllerProvider);
    expect(
      Matrix4.tryInvert(selectionTransform(p, p.layerById(id)!, Duration.zero)),
      isNull,
    );
    e.editPositionZ(id, Duration.zero, -300);
    await tester.pumpAndSettle();
    expect(_conteudoDa(id), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
