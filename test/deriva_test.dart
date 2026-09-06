import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/projects/application/modelos_empacotados.dart';
import 'package:aurea/src/features/projects/domain/deriva_template.dart';

/// A DERIVA e um filme de tres tomadas numa cena so: o que este teste
/// segura e justamente isso — que o corte seja de CAMERA (o corpo nao
/// reinicia no corte), que a luz seja de espaco (uma fonte dura e
/// ambiente quase zero) e que a cena caiba no aparelho.
void main() {
  final project = buildDerivaTemplate();
  final cena = project.layers.whereType<Scene3DLayer>().single;

  test('tres tomadas, uma cena, cortes marcados', () {
    expect(cena.shots, hasLength(3));
    expect(cena.extraCameras, hasLength(2));
    final ids = [cena.camera.id, ...cena.extraCameras.map((c) => c.id)];
    expect(ids.toSet().length, 3);
    for (var i = 0; i < 3; i++) {
      expect(cena.shots[i].cameraId, ids[i]);
      expect(
        cena.shots[i].time.inMilliseconds,
        (derivaTomadas[i] * 1000).round(),
      );
    }
    // Os marcadores levam a pessoa a cada tomada na timeline.
    expect(project.markers, hasLength(3));
    // Lentes diferentes: e o que faz as tomadas nao parecerem a mesma.
    final lentes = [
      cena.camera.focalLength.base,
      ...cena.extraCameras.map((c) => c.focalLength.base),
    ];
    expect(lentes.toSet().length, 3);
    expect(lentes.reduce((a, b) => a > b ? a : b), greaterThan(60));
  });

  test('o corpo nao reinicia no corte: uma trajetoria so', () {
    final astro = cena.scene.nodeById('deriva_astronauta')!;
    for (final s in derivaTomadas) {
      final t = Duration(milliseconds: (s * 1000).round());
      final esperado = astronautaEm(s);
      expect(astro.x.valueAt(t), closeTo(esperado.x, 1.5));
      expect(astro.y.valueAt(t), closeTo(esperado.y, 1.5));
      expect(astro.z.valueAt(t), closeTo(esperado.z, 1.5));
    }
    // Girando sem parar nos tres eixos, do comeco ao fim.
    const fim = Duration(seconds: 18);
    expect(astro.rotX.valueAt(fim) - astro.rotX.valueAt(Duration.zero),
        greaterThan(90));
    expect(astro.rotY.valueAt(fim) - astro.rotY.valueAt(Duration.zero),
        lessThan(-90));
    expect(astro.rotZ.valueAt(fim) - astro.rotZ.valueAt(Duration.zero),
        greaterThan(50));
  });

  test('luz de vacuo: uma fonte dura, sombra sem preenchimento', () {
    final scene = cena.scene;
    expect(scene.fogDensity, 0, reason: 'no vacuo nao ha neblina');
    expect(scene.ambient, lessThan(.08), reason: 'a sombra tem de ser preta');
    expect(scene.background, isNotNull);
    final sol = scene.lights.firstWhere((l) => l.id == 'deriva_sol');
    expect(sol.castsShadow, isTrue);
    expect(sol.intensity.valueAt(Duration.zero), greaterThan(2.5));
    // Toda outra luz e um sussurro perto do sol.
    for (final l in scene.lights.where((l) => l.id != 'deriva_sol')) {
      expect(l.intensity.valueAt(Duration.zero), lessThan(.5));
      expect(l.castsShadow, isFalse);
    }
    // O sol tambem se ve: um disco emissivo, para o brilho pegar nele.
    final disco = scene.nodeById('deriva_sol')!;
    final mats = disco.modelAsset!.data['materials'] as List;
    expect(mats.every((m) => (m['emissive'] as num) > 0), isTrue);
  });

  test('abre e fecha no preto', () {
    final o = cena.opacity;
    expect(o.valueAt(Duration.zero), 0);
    expect(o.valueAt(const Duration(seconds: 9)), 1);
    expect(o.valueAt(const Duration(seconds: 18)), 0);
  });

  test('cabe no orcamento e e determinista', () {
    final total = derivaTriangles(project);
    expect(total, lessThanOrEqualTo(derivaTriangleBudget),
        reason: '$total triangulos por quadro');
    expect(derivaTriangles(buildDerivaTemplate()), total);
    final ids = cena.scene.nodes.map((n) => n.id).toList();
    expect(ids.toSet().length, ids.length);
  });

  test('com o astronauta importado continua abaixo do teto', () async {
    final modelo = await carregarAstronautaDe('assets/models/monolito');
    final p = buildDerivaTemplate(astronauta: modelo);
    final total = derivaTriangles(p);
    expect(total, lessThanOrEqualTo(derivaTriangleBudget),
        reason: '$total triangulos');
    final c = p.layers.whereType<Scene3DLayer>().single;
    expect(c.scene.nodeById('deriva_astronauta')!.modelAsset, same(modelo));
  });

  test('vai e volta do JSON', () {
    final json = jsonEncode(projectToJson(project));
    final volta = projectFromJson(jsonDecode(json) as Map<String, dynamic>);
    expect(derivaTriangles(volta), derivaTriangles(project));
    final c = volta.layers.whereType<Scene3DLayer>().single;
    expect(c.shots, hasLength(3));
    expect(c.extraCameras, hasLength(2));
  });
}
