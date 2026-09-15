// O IPHONE 3D PARAMETRICO: um aparelho de nos comuns da cena — cada
// peca editavel de verdade — pendurado num nulo proprio e vinculado a um
// nulo da linha do tempo.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  (ProviderContainer, EditorController) montar() {
    final c = ProviderContainer();
    final e = c.read(editorControllerProvider.notifier);
    e.openProject(
      VideoProject(name: 'p', createdAt: DateTime(2026), layers: const []),
    );
    return (c, e);
  }

  test('monta o aparelho inteiro: nulo + corpo + tela + ilha + 3 lentes',
      () async {
    final (c, e) = montar();
    addTearDown(c.dispose);
    final raiz = await e.addIphone3D(Duration.zero);
    expect(raiz, isNotNull);

    final cena = e.state.layers.whereType<Scene3DLayer>().single;
    final nos = cena.scene.nodes;
    final nulo = cena.scene.nodeById(raiz!)!;
    expect(nulo.isNull, isTrue);
    expect(nulo.name, 'iPhone');

    final partes = [for (final n in nos) if (n.parentId == raiz) n];
    expect(partes.map((n) => n.name).toSet(), {
      'Corpo',
      'Tela',
      'Ilha das câmeras',
      'Lente 1',
      'Lente 2',
      'Lente 3',
    });

    // As pecas extrudadas persistem pelo CONTORNO (a malha se refaz na
    // leitura do projeto) e ja nascem com volume.
    final corpo = partes.singleWhere((n) => n.name == 'Corpo');
    expect(corpo.outline, isNotNull);
    expect(corpo.mesh!.verts, isNotEmpty);

    // A tela e SEM LUZ (textura aparece no brilho cheio) e fica na
    // frente do corpo; a ilha fica atras.
    final tela = partes.singleWhere((n) => n.name == 'Tela');
    expect(tela.material.kind, MaterialKind.unlit);
    expect(tela.z.valueAt(Duration.zero), greaterThan(0));
    final ilha = partes.singleWhere((n) => n.name == 'Ilha das câmeras');
    expect(ilha.z.valueAt(Duration.zero), lessThan(0));

    // O nulo da composicao nasceu vinculado ao nulo da cena.
    final nuloDaLinha =
        e.state.layers.firstWhere((l) => l.name == 'Nulo 3D · iPhone');
    expect(nulo.compParentLayerId, nuloDaLinha.id);

    // Metal precisa do que refletir: cena sem panorama ganha o estudio.
    expect(
      e.state.layers.whereType<Scene3DLayer>().single.scene.environment,
      EnvironmentKind.estudioMetal,
    );
  });

  test('desfazer remove o aparelho num passo so', () async {
    final (c, e) = montar();
    addTearDown(c.dispose);
    await e.addIphone3D(Duration.zero);
    expect(e.state.layers.whereType<Scene3DLayer>(), isNotEmpty);
    e.undo();
    expect(e.state.layers, isEmpty);
  });

  test('a tela aceita a textura de uma camada de imagem', () async {
    final (c, e) = montar();
    addTearDown(c.dispose);
    final raiz = await e.addIphone3D(Duration.zero);
    final cena = e.state.layers.whereType<Scene3DLayer>().single;
    final tela = cena.scene.nodes
        .singleWhere((n) => n.parentId == raiz && n.name == 'Tela');
    // O caminho da ficha do no: textureLayerId + imagePath da camada.
    e.updateSceneNode(
      cena.id,
      tela.id,
      (n) => n.copyWith(
        material: n.material.copyWith(
          textureLayerId: 'img-1',
          imagePath: '/fotos/still.png',
        ),
      ),
    );
    final depois = e.state.layers
        .whereType<Scene3DLayer>()
        .single
        .scene
        .nodeById(tela.id)!;
    expect(depois.material.imagePath, '/fotos/still.png');
    expect(depois.material.textureLayerId, 'img-1');
  });
}
