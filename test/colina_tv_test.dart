import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/projects/domain/colina_tv_template.dart';

/// A COLINA tem de caber num iPhone 13: o orcamento de triangulos e o
/// que segura a cena abaixo do que a CPU renderiza a 30 fps, e o
/// projeto tem de ir e voltar do JSON inteiro (texturas embutidas,
/// instancias, camera com profundidade de campo).
void main() {
  final project = buildColinaTvTemplate();
  final cena = project.layers.whereType<Scene3DLayer>().single;

  test('cabe no orcamento de triangulos por quadro', () {
    final total = colinaTriangles(project);
    expect(total, lessThanOrEqualTo(colinaTriangleBudget),
        reason: '$total triangulos por quadro');
    expect(total, greaterThan(6000), reason: 'a cena ficou vazia demais');
  });

  test('ids unicos e o mesmo projeto a cada abertura', () {
    final ids = project.layers.map((l) => l.id).toList();
    expect(ids.toSet().length, ids.length);
    final nos = cena.scene.nodes.map((n) => n.id).toList();
    expect(nos.toSet().length, nos.length);
    // Determinista: nenhum random, nenhum relogio na geometria.
    final outra = buildColinaTvTemplate();
    expect(colinaTriangles(outra), colinaTriangles(project));
    expect(outra.layers.length, project.layers.length);
  });

  test('a TV fica em cima do morro e a camera olha para ela', () {
    final tv = cena.scene.nodeById('colina_tv')!;
    expect(tv.y.valueAt(Duration.zero), greaterThan(colinaAltura(0, 40)));
    final cam = cena.camera;
    for (final s in [0, 3, 6]) {
      final t = Duration(seconds: s);
      expect(cam.poiY.valueAt(t), closeTo(tv.y.valueAt(t), 80));
      expect(cam.dof.enabled, isTrue);
      expect(cam.dof.focusDistance.valueAt(t), greaterThan(500));
    }
    // Aproxima: a distancia cai ao longo do plano.
    final d0 = cam.dof.focusDistance.valueAt(Duration.zero);
    final d6 = cam.dof.focusDistance.valueAt(const Duration(seconds: 6));
    expect(d6, lessThan(d0 * .8));
  });

  test('vai e volta do JSON com as texturas embutidas', () {
    final json = jsonEncode(projectToJson(project));
    expect(json.length, lessThan(6 * 1024 * 1024),
        reason: 'projeto salvo de ${json.length ~/ 1024} KB');
    final volta = projectFromJson(jsonDecode(json) as Map<String, dynamic>);
    expect(volta.layers.length, project.layers.length);
    final cena2 = volta.layers.whereType<Scene3DLayer>().single;
    expect(cena2.scene.nodes.length, cena.scene.nodes.length);
    expect(colinaTriangles(volta), colinaTriangles(project));
    final grama = cena2.scene.nodeById('colina_terreno')!;
    final imagem =
        (grama.modelAsset!.data['materials'] as List).first['image'] as String;
    expect(imagem, startsWith('data:image/png;base64,'));
  });
}
