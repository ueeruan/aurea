import 'dart:math' as math;

import 'package:flutter/widgets.dart';

import 'layer.dart';
import 'video_project.dart';

/// The editor overlay uses the same 2D transform order as the content:
/// position, pivot, rotation, skew, scale, inverse pivot.
Matrix4 selectionTransform(VideoProject project, Layer layer, Duration time) {
  final local = layer.localTime(time);
  final effective = effectiveTransform(project, layer, time);
  final ownScale = layer.scaleX.valueAt(local);
  final ratio = ownScale.abs() < 1e-6 ? 1.0 : effective.scale / ownScale;
  var sx = ownScale * ratio;
  var sy = layer.scaleY.valueAt(local) * ratio;
  var pos = effective.pos;
  if (layer.is3D || effective.z != 0) {
    // A MESMA projecao do palco: encolhe e vai para o ponto de fuga.
    final vista = projetarProfundidade(
      project,
      pos,
      effective.z,
      ortografica: cameraAtivaEm(project, time)?.opcoes.ortografica ?? false,
    );
    // Passou da camera: nao desenha, entao nao se toca.
    if (vista == null) return Matrix4.zero();
    pos = vista.pos;
    sx *= vista.escala;
    sy *= vista.escala;
  }
  final pivot = layer.pivot.valueAt(local);
  final tilt =
      (effective.rotX != 0 || effective.rotY != 0) &&
      layer is! ParticlesLayer &&
      layer is! Element3DLayer;
  final matrix = Matrix4.identity()..translateByDouble(pos.dx, pos.dy, 0, 1);
  if (tilt) {
    matrix.multiply(
      Matrix4.identity()
        ..setEntry(3, 2, -1 / 1200)
        ..rotateZ(effective.rot * math.pi / 180)
        ..rotateY(effective.rotY * math.pi / 180)
        ..rotateX(effective.rotX * math.pi / 180),
    );
  }
  matrix.translateByDouble(pivot.dx, pivot.dy, 0, 1);
  if (!tilt) matrix.rotateZ(effective.rot * math.pi / 180);
  matrix
    ..multiply(
      Matrix4.skew(
        layer.skewX.valueAt(local) * math.pi / 180,
        layer.skewY.valueAt(local) * math.pi / 180,
      ),
    )
    ..scaleByDouble(sx, sy, 1, 1)
    ..translateByDouble(-pivot.dx, -pivot.dy, 0, 1);
  // Hit testing inverts the projected layer plane, not a point at world Z=0.
  // Preserve x/y/w as a homography; flatten the unused Z row and column.
  for (final i in [0, 1, 3]) {
    matrix.setEntry(2, i, 0);
    matrix.setEntry(i, 2, 0);
  }
  matrix.setEntry(2, 2, 1);
  return matrix;
}
