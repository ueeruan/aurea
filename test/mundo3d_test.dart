import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/world3d_painter.dart';

Element3DLayer _cubo(String nome,
        {double size = 200,
        int material = 0,
        Element3DKind kind = Element3DKind.cube}) =>
    Element3DLayer(
      name: nome,
      startTime: Duration.zero,
      duration: const Duration(seconds: 3),
      kind: kind,
      size: size,
      material: material,
    );

void main() {
  group('mundo 3D compartilhado', () {
    test('dois solidos viram uma lista so, com profundidade por triangulo',
        () {
      final perto = World3DItem(
          layer: _cubo('perto'), center: const Offset(500, 500), z: -200);
      final longe = World3DItem(
          layer: _cubo('longe'), center: const Offset(560, 520), z: 400);
      final tris =
          World3DPainter.build([perto, longe], const Size(1000, 1000));
      expect(tris.where((t) => t.item == 0), isNotEmpty);
      expect(tris.where((t) => t.item == 1), isNotEmpty);
      double media(int item) {
        final ts = tris.where((t) => t.item == item).toList();
        return ts.fold(0.0, (a, t) => a + t.depth) / ts.length;
      }

      // O de perto tem profundidade menor: pintado por ultimo depois da
      // ordenacao (fundo primeiro).
      expect(media(0), lessThan(media(1)));
      final ordenado = [...tris]..sort((a, b) => b.depth.compareTo(a.depth));
      expect(ordenado.last.item, 0);
      expect(ordenado.first.item, 1);
    });

    test('com mais de um solido as faces sao subdivididas', () {
      final a = World3DItem(layer: _cubo('a'), center: const Offset(300, 300));
      final b = World3DItem(layer: _cubo('b'), center: const Offset(360, 300));
      final sozinho = World3DPainter.build([a], const Size(600, 600));
      final juntos = World3DPainter.build([a, b], const Size(600, 600));
      // Juntos, cada solido tem mais triangulos que sozinho.
      expect(juntos.where((t) => t.item == 0).length,
          greaterThan(sozinho.length));
      // Toda aresta de borda do poligono original continua marcada.
      expect(juntos.any((t) => t.edges != 0), isTrue);
    });

    test('materiais: vidro e translucido, degrade tem as pontas certas', () {
      final vidro = World3DItem(
          layer: _cubo('v', material: 2), center: const Offset(300, 300));
      final tris = World3DPainter.build([vidro], const Size(600, 600));
      expect(tris.every((t) => t.colors.every((c) => c.a < 1)), isTrue);

      final solido = World3DItem(
          layer: _cubo('s'), center: const Offset(300, 300));
      final ts = World3DPainter.build([solido], const Size(600, 600));
      expect(ts.every((t) => t.colors.every((c) => c.a == 1)), isTrue);

      const a = Color(0xFFFF0000), b = Color(0xFF0000FF);
      expect(World3DPainter.gradientAt(const [a, b], 0), a);
      expect(World3DPainter.gradientAt(const [a, b], 1), b);
    });

    test('esfera sai lisa: cores diferentes por vertice numa mesma face', () {
      final esfera = World3DItem(
          layer: _cubo('e', kind: Element3DKind.sphere, material: 1),
          center: const Offset(300, 300));
      final tris = World3DPainter.build([esfera], const Size(600, 600));
      expect(tris.any((t) => t.colors.toSet().length > 1), isTrue);
    });

    test('pinta sem erro com todos os materiais e imagem ausente', () {
      for (var m = 0; m < 5; m++) {
        final rec = ui.PictureRecorder();
        final canvas = Canvas(rec);
        World3DPainter(items: [
          World3DItem(
              layer: _cubo('a', material: m).copyElement3D(edges: true),
              center: const Offset(300, 300),
              rotXDeg: 30,
              rotYDeg: 40,
              selected: true),
          World3DItem(
              layer: _cubo('b', material: m, kind: Element3DKind.sphere),
              center: const Offset(340, 320),
              z: 80),
        ]).paint(canvas, const Size(600, 600));
        rec.endRecording().dispose();
      }
    });

    test('material, degrade e brilho vao e voltam do JSON', () {
      final l = _cubo('x', material: 3).copyElement3D(
        gradient: const [Color(0xFF111111), Color(0xFF222222)],
        shininess: 0.8,
      );
      final projeto = VideoProject(
        name: 'p',
        createdAt: DateTime(2026, 1, 1),
        layers: [l],
      );
      final volta = projectFromJson(projectToJson(projeto)).layers.single
          as Element3DLayer;
      expect(volta.material, 3);
      expect(volta.gradient, const [Color(0xFF111111), Color(0xFF222222)]);
      expect(volta.shininess, 0.8);
      final c = volta.copyLayer(is3D: true);
      expect(c.material, 3);
      expect(c.shininess, 0.8);
    });
  });
}
