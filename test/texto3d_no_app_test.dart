// TEXTO 3D NO APP: a malha vira modelo com os tres metais e entra na cena
// presa a um nulo (pedido de 14/09/2026, "identico ao Element 3D").
import 'dart:io';

import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/fonte_truetype.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart'
    show cenaComNulosDaComposicao;
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/fonte_de_malha.dart';
import 'package:aurea/src/features/editor/application/motor3d_nativo.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('o texto vira modelo com frente, chanfro e lateral em metal', () {
    final fonte = FonteTrueType.ler(
      File('assets/templates/dnyx/AureaMotionSans.ttf').readAsBytesSync(),
    );
    const t = Texto3D(texto: 'ELEMENT');
    final malha = malhaDoTexto3D(
      disporTexto3D(t, fonte),
      t,
      fonte.unidadesPorEm,
    );
    for (final estilo in EstiloDoTexto3D.values) {
      final m = modeloDoTexto3D(malha, 'ELEMENT', estilo);
      expect(m.primitives, hasLength(3), reason: estilo.name);
      expect(m.triangleCount, malha.triangulos);
      final mats = m.data['materials'] as List;
      expect(mats, hasLength(3));
      if (estilo != EstiloDoTexto3D.brancoFosco) {
        for (final mat in mats) {
          expect((mat as Map)['metallic'], 1.0, reason: estilo.name);
        }
      }
      final p = m.primitives.first as Map;
      expect((p['positions'] as List).length, (p['normals'] as List).length);
      expect((p['lods'] as List).single, isNotEmpty, reason: 'rascunho');
    }
  });

  test('estudio metal: fundo quase preto e softbox muito acima de 1', () {
    final fundo = environmentColor(EnvironmentKind.estudioMetal, 0, -.2, 1);
    expect(fundo.$1, lessThan(.1));
    final luz = environmentColor(EnvironmentKind.estudioMetal, -.45, .55, .7);
    expect(luz.$1, greaterThan(5));
  });

  test('no da cena preso ao nulo da composicao segue o nulo', () {
    final nulo = NullLayer(
      id: 'nulo',
      name: 'nulo',
      startTime: Duration.zero,
      duration: const Duration(seconds: 4),
      position: AnimatedOffset(const Offset(1060, 490)),
    );
    final no = SceneNode(
      name: 'texto',
      isNull: true,
      compParentLayerId: 'nulo',
    );
    final cena = Scene3DLayer(
      id: 'cena',
      name: 'cena',
      startTime: Duration.zero,
      duration: const Duration(seconds: 4),
      scene: Scene3D(nodes: [no]),
    );
    final projeto = VideoProject(
      name: 'p',
      createdAt: DateTime(2026, 9, 14),
      aspectRatio: 16 / 9,
      resolutionHeight: 1080,
      layers: [nulo, cena],
    );
    final resolvida = cenaComNulosDaComposicao(
      projeto,
      cena,
      Duration.zero,
      Duration.zero,
    ).nodes.single;
    // Centro 960x540: o nulo esta 100 px a direita e 50 px ACIMA.
    expect(resolvida.x.valueAt(Duration.zero), closeTo(100, 1e-9));
    expect(resolvida.y.valueAt(Duration.zero), closeTo(50, 1e-9));
    final volta = projectFromJson(projectToJson(projeto));
    final lida =
        (volta.layers.whereType<Scene3DLayer>().single).scene.nodes.single;
    expect(lida.compParentLayerId, 'nulo');
  });

  test(
    'botao cria texto, camada na timeline e malha para o motor novo',
    () async {
      final container = ProviderContainer();
      try {
        final controller = container.read(editorControllerProvider.notifier);
        controller.openProject(VideoProject.empty('texto 3d'));

        final nodeId = await controller.addTexto3D(
          Duration.zero,
          'AUREA',
          EstiloDoTexto3D.cromo,
          familia: 'fonte que nao existe',
        );

        expect(nodeId, isNotNull, reason: 'deve cair na fonte interna');
        final project = container.read(editorControllerProvider);
        final scene = project.layers.whereType<Scene3DLayer>().single;
        expect(scene.name, 'Texto 3D · AUREA');
        expect(project.layers.whereType<NullLayer>(), isEmpty);
        final node = scene.scene.nodeById(nodeId!)!;
        expect(node.modelAsset, isNotNull);
        final cache = CacheDeMalhas();
        final source = cache.doNo(
          node,
          Duration.zero,
          lodDaReceita: (_) => null,
          assinaturaDoMaterial: assinaturaDoMaterial3D,
        );
        expect(source, isNotNull);
        final nativeMeshes = malhasCruas3DDe(source!);
        expect(nativeMeshes, isNotEmpty);
        expect(nativeMeshes.every((m) => m.quantidadeDeIndices >= 3), isTrue);
      } finally {
        container.dispose();
      }
    },
  );
}
