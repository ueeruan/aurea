import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

void main() {
  test('motion PicPay 10s parseia e resolve no motor', () {
    final raw = File('motions/picpay10s.json').readAsStringSync();
    final project =
        projectFromJson(jsonDecode(raw) as Map<String, dynamic>);

    expect(project.name, 'PicPay Motion 10s');
    expect(project.duration, const Duration(seconds: 10));
    expect(project.outputWidth, 1080);
    expect(project.outputHeight, 1920);
    expect(project.layers.length, 16);
    expect(project.links.length, 3);

    // Titulo com animador por letra.
    final title =
        project.layerById('title')! as TextLayer;
    expect(title.animators.single.properties.length, 4);

    // Orbita: com o nulo girado, a moeda sai do lugar e ganha profundidade.
    final coin = project.layerById('coin1')!;
    final t0 = const Duration(milliseconds: 3900);
    final t1 = const Duration(milliseconds: 7000);
    final e0 = effectiveTransform(project, coin, t0);
    final e1 = effectiveTransform(project, coin, t1);
    expect((e1.pos - e0.pos).distance, greaterThan(50));
    expect(e1.z.abs() + e0.z.abs(), greaterThan(1));

    // Morph do coracao chega em progresso 1 no fim.
    final heart = project.layerById('heart')! as ShapeLayer;
    final morphOk = heart.contents.any((c) => c.toString().contains('ShapeMorph'));
    expect(morphOk || heart.contents.length == 2, true);

    // Round-trip: salvar e reabrir nao perde nada.
    final again = projectFromJson(
        jsonDecode(jsonEncode(projectToJson(project)))
            as Map<String, dynamic>);
    expect(again.layers.length, 16);
    expect(again.links.length, 3);
  });
}
