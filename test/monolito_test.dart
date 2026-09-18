import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/projects/domain/monolito_template.dart';

/// O MONOLITO tem de caber num iPhone e sobreviver ao JSON: orcamento
/// de triangulos, ids unicos, geometria determinista, a porta como luz.
void main() {
  final project = buildMonolitoTemplate();
  final cena = project.layers.whereType<Scene3DLayer>().single;

  test('cabe no orcamento de triangulos por quadro', () {
    final total = monolitoTriangles(project);
    expect(total, lessThanOrEqualTo(monolitoTriangleBudget),
        reason: '$total triangulos por quadro');
    expect(total, greaterThan(5000));
  });

  test('ids unicos e determinista', () {
    final ids = cena.scene.nodes.map((n) => n.id).toList();
    expect(ids.toSet().length, ids.length);
    expect(monolitoTriangles(buildMonolitoTemplate()), monolitoTriangles(project));
  });

  test('a porta e uma luz spot magenta com sombra, e ha neblina', () {
    final spot = cena.scene.lights.firstWhere((l) => l.kind == Light3DKind.spot);
    expect(spot.castsShadow, isTrue);
    expect(spot.color.r, greaterThan(spot.color.g));
    expect(spot.color.b, greaterThan(spot.color.g));
    expect(cena.scene.fogDensity, greaterThan(0));
    // A pedra da porta e emissiva: e o que o bloom acende.
    final bloco = cena.scene.nodeById('monolito_bloco')!;
    final materiais = bloco.modelAsset!.data['materials'] as List;
    expect(materiais.any((m) => (m['emissive'] as num) > 0), isTrue);
  });

  test('o astronauta flutua diante da porta e a camera olha para ele', () {
    final astro = cena.scene.nodeById('monolito_astronauta')!;
    expect(astro.modelAsset, isNotNull);
    final y0 = astro.y.valueAt(Duration.zero);
    final y2 = astro.y.valueAt(const Duration(milliseconds: 1300));
    expect(y0, greaterThan(60));
    expect(y0, isNot(closeTo(y2, .5)), reason: 'tem de subir e descer');
    final cam = cena.camera;
    for (final s in [0, 8, 16]) {
      final t = Duration(seconds: s);
      expect(cam.dof.focusDistance.valueAt(t), greaterThan(300));
      expect((cam.poiX.valueAt(t) - astro.x.valueAt(t)).abs(), lessThan(400));
    }
  });

  test('fica abaixo do teto da GPU, com tudo gerado em codigo', () async {
    // OS TRES SCANS SAIRAM DO APLICATIVO (astronauta, portal e arvore):
    // eram modelos de terceiros viajando dentro do APK. O explorador, a
    // floresta e o bloco sao malha propria — e o Portal, que era um
    // modelo voxel importado, deixou de existir junto com o arquivo dele.
    final p = buildMonolitoTemplate();
    final total = monolitoTriangles(p);
    expect(total, lessThanOrEqualTo(monolitoTriangleBudgetComModelos),
        reason: '$total triangulos');
    final c = p.layers.whereType<Scene3DLayer>().single;
    expect(c.scene.nodeById('monolito_portal'), isNull);
    final astronauta = c.scene.nodeById('monolito_astronauta')!;
    expect(astronauta.modelAsset, isNotNull);
    expect(astronauta.modelAsset!.triangleCount, greaterThan(200));
    // A FLORESTA INTEIRA E PROCEDURAL: nao ha mais nenhum no de scan.
    expect(
      c.scene.nodes.where((n) => n.id.contains('arvore_real')),
      isEmpty,
    );
  });

  test('vai e volta do JSON', () {
    final json = jsonEncode(projectToJson(project));
    expect(json.length, lessThan(8 * 1024 * 1024));
    final volta = projectFromJson(jsonDecode(json) as Map<String, dynamic>);
    expect(monolitoTriangles(volta), monolitoTriangles(project));
  });
}
