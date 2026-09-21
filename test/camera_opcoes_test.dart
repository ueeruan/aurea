// OPCOES DA CAMERA (v1.1.1): perspectiva ou ortografica, angulo de visao
// ligado a lente (com o desenho do cone), desfoque de foco pela distancia
// ao olho da camera e neblina — no palco, na selecao e no arquivo.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/camera.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'ui/paineis_3d/banco.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

ProviderContainer _container() {
  final c = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
  );
  addTearDown(c.dispose);
  return c;
}

CameraLayer _camera(ProviderContainer c) =>
    c.read(editorControllerProvider).layers.whereType<CameraLayer>().first;

void main() {
  test('angulo de visao e lente sao a mesma coisa na largura', () {
    expect(anguloDaLente(1920, 960), closeTo(90, 1e-9));
    expect(lenteDoAngulo(1920, 90), closeTo(960, 1e-9));
    expect(anguloDaLente(1080, lenteDoAngulo(1080, 37)), closeTo(37, 1e-9));
  });

  test('foco: nitido na faixa, desfoca reto fora dela, com teto', () {
    final o = OpcoesDaCamera(
      focoLigado: true,
      distanciaDoFoco: AnimatedDouble(1200),
      profundidadeDeCampo: AnimatedDouble(200),
      intensidadeDoFoco: AnimatedDouble(10),
    );
    expect(o.desfoqueEm(1200, Duration.zero), 0);
    expect(o.desfoqueEm(1300, Duration.zero), 0, reason: 'meia faixa');
    expect(o.desfoqueEm(1500, Duration.zero), closeTo(10, 1e-9));
    expect(o.desfoqueEm(900, Duration.zero), closeTo(10, 1e-9));
    expect(o.desfoqueEm(99999, Duration.zero), 40);
    expect(o.copyWith(focoLigado: false).desfoqueEm(5000, Duration.zero), 0);
  });

  test('neblina: de perto a longe, e a matriz puxa para a cor sem mexer no alfa', () {
    final o = OpcoesDaCamera(
      neblinaLigada: true,
      neblinaPerto: AnimatedDouble(1000),
      neblinaLonge: AnimatedDouble(3000),
    );
    expect(o.neblinaEm(800, Duration.zero), 0);
    expect(o.neblinaEm(2000, Duration.zero), closeTo(.5, 1e-9));
    expect(o.neblinaEm(9000, Duration.zero), 1);
    final m = matrizDaNeblina(const Color(0xFFFF0000), .5);
    expect(m[0], .5);
    expect(m[4], closeTo(127.5, 1e-9));
    expect(m[18], 1, reason: 'alfa intacto');
  });

  test('ortografica: a profundidade nao encolhe nem corre para o centro', () {
    final c = _container();
    final p = c.read(editorControllerProvider);
    const pos = Offset(300, 200);
    final perspectiva = projetarProfundidade(p, pos, 1200)!;
    expect(perspectiva.escala, closeTo(.5, 1e-9));
    final orto = projetarProfundidade(p, pos, 1200, ortografica: true)!;
    expect(orto.escala, 1);
    expect(orto.pos, pos);
  });

  test('as opcoes voltam do arquivo e entram na barra da camera', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addCameraLayer(Duration.zero);
    final id = _camera(c).id;
    e.atualizarOpcoesDaCamera(
      id,
      (o) => o.copyWith(
        ortografica: true,
        focoLigado: true,
        distanciaDoFoco: AnimatedDouble(1200)
            .withKeyframe(Duration.zero, 1000)
            .withKeyframe(const Duration(seconds: 2), 3000),
        neblinaLigada: true,
        corDaNeblina: const Color(0xFF223344),
        neblinaLonge: AnimatedDouble(7000),
      ),
    );
    expect(
      _camera(c).moduleTimesUs,
      contains(const Duration(seconds: 2).inMicroseconds),
    );
    final volta = projectFromJson(projectToJson(c.read(editorControllerProvider)));
    final o = volta.layers.whereType<CameraLayer>().single.opcoes;
    expect(o.ortografica, isTrue);
    expect(o.focoLigado, isTrue);
    expect(o.distanciaDoFoco.valueAt(const Duration(seconds: 1)), closeTo(2000, 1e-9));
    expect(o.neblinaLigada, isTrue);
    expect(o.corDaNeblina, const Color(0xFF223344));
    expect(o.neblinaLonge.valueAt(Duration.zero), 7000);
    // Duplicar leva as opcoes junto.
    expect(_camera(c).duplicated().opcoes.focoLigado, isTrue);
  });

  testWidgets('no palco: camada 3D longe do foco desfoca e pega neblina', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'Perto');
    final perto = c.read(editorControllerProvider).layers.first.id;
    e.toggle3D(perto);
    e.addShapeLayer(Duration.zero, name: 'Longe');
    final longe = c.read(editorControllerProvider).layers.first.id;
    e.toggle3D(longe);
    e.editPositionZ(longe, Duration.zero, 2000);
    e.addCameraLayer(Duration.zero);
    final cam = _camera(c).id;
    e.atualizarOpcoesDaCamera(
      cam,
      (o) => o.copyWith(
        focoLigado: true,
        profundidadeDeCampo: AnimatedDouble(400),
        neblinaLigada: true,
        neblinaPerto: AnimatedDouble(1500),
        neblinaLonge: AnimatedDouble(4000),
      ),
    );
    c.read(selectedLayerProvider.notifier).state = null;
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: const MaterialApp(home: EditorScreen()),
      ),
    );
    await tester.pumpAndSettle();
    expect(find.byKey(ValueKey('foco-$longe')), findsOneWidget);
    expect(find.byKey(ValueKey('neblina-$longe')), findsOneWidget);
    expect(find.byKey(ValueKey('foco-$perto')), findsNothing, reason: 'no plano nitido');
    expect(find.byKey(ValueKey('neblina-$perto')), findsNothing);

    e.atualizarOpcoesDaCamera(cam, (o) => o.copyWith(focoLigado: false, neblinaLigada: false));
    await tester.pumpAndSettle();
    expect(find.byKey(ValueKey('foco-$longe')), findsNothing);
    expect(find.byKey(ValueKey('neblina-$longe')), findsNothing);
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });

  testWidgets('o painel: ortografica, angulo que muda a lente, foco e neblina', (
    tester,
  ) async {
    final c = containerNovo();
    controladorDe(c).addCameraLayer(Duration.zero);
    final id = _camera(c).id;
    await montar(tester, c, (_) => PainelCamera(layerId: id));

    // Lente: o angulo digitado no teclado do app vira a lente.
    await tester.tap(find.byKey(const ValueKey('valor-camera-angulo')));
    await tester.pumpAndSettle();
    await tester.enterText(find.byKey(const ValueKey('valor-campo')), '90');
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    final largura = c.read(editorControllerProvider).outputWidth.toDouble();
    expect(_camera(c).zoom.valueAt(Duration.zero), closeTo(largura / 2, 1e-6));

    await tester.tap(find.byKey(const ValueKey('camera-ortografica')));
    await tester.pumpAndSettle();
    expect(_camera(c).opcoes.ortografica, isTrue);
    expect(find.byKey(const ValueKey('prop-camera-angulo')), findsNothing);

    await tocarNaAba(tester, 'camera', 1);
    await tester.tap(find.byKey(const ValueKey('interruptor-camera-foco')));
    await tester.pumpAndSettle();
    expect(_camera(c).opcoes.focoLigado, isTrue);
    expect(
      find.byKey(const ValueKey('prop-camera-foco-distancia')),
      findsOneWidget,
    );

    await tocarNaAba(tester, 'camera', 2);
    await tester.tap(find.byKey(const ValueKey('interruptor-camera-neblina')));
    await tester.pumpAndSettle();
    expect(_camera(c).opcoes.neblinaLigada, isTrue);
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });
}
