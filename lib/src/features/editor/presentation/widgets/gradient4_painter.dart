import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

/// GRADIENTE DE QUATRO CORES: uma cor em cada canto, misturadas em
/// bilinear. E o degrade "de cartaz" que o gradiente de dois pontos nao
/// faz — quatro cantos dao a mancha diagonal que se ve em capa de
/// musica e em fundo de motion.
///
/// Dois triangulos com cor por vertice: a GPU interpola. Zero custo.
class Gradient4Painter extends CustomPainter {
  const Gradient4Painter({
    required this.topLeft,
    required this.topRight,
    required this.bottomLeft,
    required this.bottomRight,
    this.opacity = 1,
  });

  final Color topLeft;
  final Color topRight;
  final Color bottomLeft;
  final Color bottomRight;
  final double opacity;

  @override
  void paint(Canvas canvas, Size size) {
    if (size.isEmpty) return;
    final w = size.width, h = size.height;
    final positions = Float32List.fromList([
      0, 0, w, 0, 0, h, //
      w, 0, w, h, 0, h,
    ]);
    int cor(Color c) => c.withValues(alpha: c.a * opacity).toARGB32();
    final colors = Int32List.fromList([
      cor(topLeft), cor(topRight), cor(bottomLeft), //
      cor(topRight), cor(bottomRight), cor(bottomLeft),
    ]);
    canvas.drawVertices(
      ui.Vertices.raw(ui.VertexMode.triangles, positions, colors: colors),
      BlendMode.srcOver,
      Paint(),
    );
  }

  @override
  bool shouldRepaint(Gradient4Painter old) =>
      old.topLeft != topLeft ||
      old.topRight != topRight ||
      old.bottomLeft != bottomLeft ||
      old.bottomRight != bottomRight ||
      old.opacity != opacity;
}
