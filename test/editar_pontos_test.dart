// EDITAR PONTOS (v1.1.1): regua do contorno, alca de entrada ou saida
// com alcas iguais, mover o contorno inteiro, varios contornos na mesma
// forma e a aba de keyframes com cravar e pular entre formas.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/path_edit.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/points_panel.dart';
import 'package:aurea/src/features/editor/presentation/widgets/mask_node_editor.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

BezierPath _quadrado({bool suave = false}) => BezierPath(
  vertices: [
    for (final p in const [Offset(-100, -100), Offset(100, -100), Offset(100, 100), Offset(-100, 100)])
      PathVertex(p: p, inT: const Offset(-20, 0), outT: const Offset(30, 0), corner: !suave),
  ],
);

void main() {
  test('alca de ponto suave: a oposta segue a direcao; com alcas iguais, o tamanho', () {
    final c = _quadrado(suave: true);
    final so = moveHandle(c, 0, Handle.saida, const Offset(-100, -60));
    expect(so.vertices[0].outT, const Offset(0, 40));
    expect(so.vertices[0].inT.dx, closeTo(0, 1e-9));
    expect(so.vertices[0].inT.dy, closeTo(-20, 1e-9), reason: 'tamanho antigo');
    final iguais = moveHandle(c, 0, Handle.saida, const Offset(-100, -60), alcasIguais: true);
    expect(iguais.vertices[0].inT, const Offset(0, -40));
    // Canto continua independente.
    final canto = moveHandle(_quadrado(), 0, Handle.entrada, const Offset(-150, -100), alcasIguais: true);
    expect(canto.vertices[0].outT, const Offset(30, 0));
  });

  test('mover o contorno inteiro leva todos os pontos e deixa as alcas', () {
    final movido = moverTodosOsPontos(_quadrado(), const Offset(10, -5));
    expect(movido.vertices.map((v) => v.p), [
      const Offset(-90, -105),
      const Offset(110, -105),
      const Offset(110, 95),
      const Offset(-90, 95),
    ]);
    expect(movido.vertices.first.outT, const Offset(30, 0));
  });

  test('contornos: a forma ganha um caminho novo antes da pintura', () {
    final c = ProviderContainer(
      overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
    );
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'F', contents: [
      ShapeBezier(path: AnimatedPath(_quadrado())),
      ShapeFill(),
    ]);
    final id = c.read(editorControllerProvider).layers.first.id;
    expect(e.contornosDaForma(id), hasLength(1));
    final novo = e.adicionarContorno(id)!;
    expect(e.contornosDaForma(id), hasLength(2));
    final itens = (c.read(editorControllerProvider).layerById(id)! as ShapeLayer).contents;
    expect(itens.indexWhere((i) => i.id == novo), lessThan(itens.indexWhere((i) => i is ShapeFill)));
  });

  testWidgets('o painel: regua seleciona, alca de entrada, contorno novo, aba de keyframes', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(400, 520);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = ProviderContainer(
      overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
    );
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    final contorno = ShapeBezier(path: AnimatedPath(_quadrado(suave: true)));
    e.addShapeLayer(Duration.zero, name: 'F', contents: [contorno, ShapeFill()]);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    c.read(pathEditTargetProvider.notifier).state = PathEditTarget(id, contorno.id, forma: true);
    c.read(editorSessionProvider.notifier).openEditPoints(contorno.id, returnTo: EditorPanel.editShape);
    final playback = PlaybackController(vsync: tester, durationOf: () => const Duration(seconds: 3));
    addTearDown(playback.dispose);
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Scaffold(
            body: Consumer(
              builder: (context, ref, _) => PointsPanel(
                playback: playback,
                layerId: id,
                itemId: ref.watch(editorSessionProvider).pointsItemId ?? contorno.id,
                onBack: () {},
              ),
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    BezierPath caminho(String itemId) => e.shapeBezierOf(id, itemId)!.path.valueAt(Duration.zero);

    await tester.tap(find.byKey(const ValueKey('pontos-no-2')));
    await tester.pumpAndSettle();
    expect(c.read(pathEditSelectedProvider), 2);

    await tester.tap(find.byKey(const ValueKey('pontos-modo-alca')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('pontos-alca-entrada')));
    await tester.pumpAndSettle();
    final antes = caminho(contorno.id).vertices[2].inT;
    final trackpad = find.byType(CustomPaint).last;
    await tester.drag(trackpad, const Offset(0, 40));
    await tester.pumpAndSettle();
    expect(caminho(contorno.id).vertices[2].inT, isNot(antes), reason: 'a alca de entrada andou');

    await tester.tap(find.byKey(const ValueKey('pontos-aba-keyframes')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('pontos-kf')));
    await tester.pumpAndSettle();
    expect(e.shapeBezierOf(id, contorno.id)!.path.hasKeyframeAt(Duration.zero), isTrue);

    await tester.tap(find.byKey(const ValueKey('pontos-aba-pontos')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('pontos-contorno-novo')));
    await tester.pumpAndSettle();
    expect(e.contornosDaForma(id), hasLength(2));
    expect(c.read(editorSessionProvider).pointsItemId, e.contornosDaForma(id).last);
    expect(c.read(pathEditModeProvider), PointsMode.add);
    expect(find.text('2/2'), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('pontos-contorno-anterior')));
    await tester.pumpAndSettle();
    expect(c.read(editorSessionProvider).pointsItemId, contorno.id);
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });
}
