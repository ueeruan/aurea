// O GIZMO DO OBJETO 3D NO PALCO: aparece sobre o objeto quando a camada
// de cena esta selecionada e SOME quando ela nao esta — sem viewport
// proprio: a camada de gizmo vive dentro do palco. As fichas da ferramenta
// (Mover, Girar, Escalar) trocam o que ele oferece.

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/widgets/gizmo_da_cena_overlay.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

ProviderContainer _container() {
  final c = ProviderContainer();
  addTearDown(c.dispose);
  return c;
}

/// Uma camada de cena com um cubo, ja selecionada.
(ProviderContainer, String, String) _cenaComCubo() {
  final c = _container();
  final e = c.read(editorControllerProvider.notifier);
  e.addScene3DLayer(Duration.zero);
  final id = c.read(editorControllerProvider).layers.single.id;
  e.addSceneNode(id, Element3DKind.cube);
  c.read(selectedLayerProvider.notifier).state = id;
  final no =
      (c.read(editorControllerProvider).layerById(id)! as Scene3DLayer)
          .scene
          .nodes
          .single;
  return (c, id, no.id);
}

void main() {
  testWidgets('o gizmo aparece sobre o objeto e some ao desselecionar', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(600, 600);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final (c, sceneId, _) = _cenaComCubo();
    final tempo = ValueNotifier<Duration>(Duration.zero);
    addTearDown(tempo.dispose);

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Scaffold(
            body: Stack(
              children: [
                const SizedBox.expand(),
                GizmoDaCenaOverlay(tempo: tempo, escala: 1),
              ],
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();

    // NAO HA VIEWPORT NOVO: o gizmo e uma camada de pintura dentro do
    // palco — nao ha segunda previa nem moldura branca.
    expect(find.byKey(const ValueKey('gizmo-da-cena')), findsOneWidget);
    // As fichas da ferramenta na mao.
    for (final m in ModoDoGizmo3D.values) {
      expect(find.byKey(ValueKey('gizmo-modo-${m.name}')), findsOneWidget);
    }

    // Desselecionar a camada apaga o gizmo inteiro.
    c.read(selectedLayerProvider.notifier).state = null;
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('gizmo-da-cena')), findsNothing);
    expect(find.byKey(const ValueKey('gizmo-modo-mover')), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('escolher a ferramenta Escalar troca o que o gizmo oferece', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(600, 600);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final (c, _, _) = _cenaComCubo();
    final tempo = ValueNotifier<Duration>(Duration.zero);
    addTearDown(tempo.dispose);

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Scaffold(
            body: Stack(
              children: [
                const SizedBox.expand(),
                GizmoDaCenaOverlay(tempo: tempo, escala: 1),
              ],
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();

    expect(c.read(modoDoGizmo3DProvider), ModoDoGizmo3D.mover);
    await tester.tap(find.byKey(const ValueKey('gizmo-modo-escalar')));
    await tester.pumpAndSettle();
    expect(c.read(modoDoGizmo3DProvider), ModoDoGizmo3D.escalar);
    expect(tester.takeException(), isNull);
  });
}
