// A ANCORA EM Z (paridade com o After Effects, 16/09).
//
// O ponto de ancoragem era 2D: uma camada 3D girava sempre em torno do
// proprio plano. No AE ele tem tres eixos, e o Z e o que permite
// orbitar um ponto a frente ou atras — a conta do carrossel de
// cartoes, do anel de logos, da capa que abre como porta.
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

ShapeLayer _forma({AnimatedDouble? pivotZ}) => ShapeLayer(
  id: 'f',
  name: 'Forma',
  startTime: Duration.zero,
  duration: const Duration(seconds: 3),
  is3D: true,
  rotationY: AnimatedDouble(35),
  pivotZ: pivotZ,
  contents: [ShapePath(primitive: ShapePrimitive.rectangle)],
);

void main() {
  test('nasce em zero: a camada gira em torno do proprio plano', () {
    expect(_forma().pivotZ.valueAt(Duration.zero), 0);
  });

  test('copyLayer leva a ancora junto', () {
    final l = _forma(pivotZ: AnimatedDouble(240));
    expect(l.copyLayer(name: 'outra').pivotZ.valueAt(Duration.zero), 240);
  });

  test('e animavel como qualquer propriedade', () {
    final l = _forma(
      pivotZ: AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(const Duration(seconds: 2), 400),
    );
    expect(l.pivotZ.valueAt(const Duration(seconds: 1)), closeTo(200, 1));
  });

  test('vai e volta do arquivo', () {
    final p = VideoProject(
      name: 'p',
      createdAt: DateTime(2026, 9, 16),
      layers: [_forma(pivotZ: AnimatedDouble(180))],
    );
    final volta = projectFromJson(projectToJson(p));
    expect(volta.layers.single.pivotZ.valueAt(Duration.zero), 180);
  });

  test('projeto SEM a chave abre com zero (nada muda no que ja existe)', () {
    final p = VideoProject(
      name: 'p',
      createdAt: DateTime(2026, 9, 16),
      layers: [_forma()],
    );
    final json = projectToJson(p);
    final camada = (json['layers'] as List).first as Map<String, dynamic>;
    expect(
      camada.containsKey('pivotZ'),
      isFalse,
      reason: 'ancora em zero nao precisa ir para o arquivo',
    );
    expect(
      projectFromJson(json).layers.single.pivotZ.valueAt(Duration.zero),
      0,
    );
  });
}
