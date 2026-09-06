import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/projects/domain/reference_rebuild_template.dart';

void main() {
  test('nova recriacao e independente, editavel e cobre os 280 quadros', () {
    final p = buildReferenceRebuildTemplate();
    expect(p.outputWidth, 720);
    expect(p.outputHeight, 1278);
    expect(p.fps, 30);
    expect(p.duration, referenceFrame(280));
    expect(p.layers.map((l) => l.id).toSet().length, p.layers.length);
    expect(p.layers.every((l) => l.id.startsWith('rebuild_')), isTrue);
    expect(p.layers.whereType<VideoLayer>(), isEmpty);
    expect(p.layers.whereType<ImageLayer>(), isEmpty);
    expect(p.layers.whereType<ShapeLayer>().length, greaterThan(40));
    expect(p.layers.whereType<Element3DLayer>().length, 4);
    for (var q = 0; q < 280; q++) {
      expect(
        p.layers.any((l) => l.activeAt(referenceFrame(q))),
        isTrue,
        reason: 'frame $q',
      );
    }
    final again = projectFromJson(
      jsonDecode(jsonEncode(projectToJson(p))) as Map<String, dynamic>,
    );
    expect(again.layers.length, p.layers.length);
    for (final l in again.layers) {
      expect(l.position.valueAt(l.duration ~/ 2).dx.isFinite, isTrue);
      if (l.matteSourceId != null) {
        expect(again.layerById(l.matteSourceId!), isNotNull);
      }
    }
  });
  test('gradiente preserva stops, centro e alcance ao salvar', () {
    final p = buildReferenceRebuildTemplate();
    final again = projectFromJson(
      jsonDecode(jsonEncode(projectToJson(p))) as Map<String, dynamic>,
    );
    final floor = again.layerById('rebuild_floor')! as ShapeLayer;
    final gradient = floor.contents.whereType<ShapeGradientFill>().single;
    expect(gradient.resolvedStops, [0, .27, .51, .65, .81, 1]);
    final light = again.layerById('rebuild_fire_light')! as ShapeLayer;
    final radial = light.contents.whereType<ShapeGradientFill>().single;
    expect(radial.center, const Offset(0, .14));
    expect(radial.radiusScale, .84);
  });
  test('gradientes invalidos recuam para espacamento uniforme', () {
    for (final stops in [
      [0.0],
      [0.0, double.nan],
      [1.0, 0.0],
      [-1.0, 1.0],
    ]) {
      final g = ShapeGradientFill(stops: stops);
      expect(g.resolvedStops, [0, 1]);
    }
    expect(ShapeGradientFill(stops: [.4, .4]).resolvedStops, [.4, .4]);
  });
}
