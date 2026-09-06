import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

/// EXTRUDE 3D pintado a partir de UMA foto da camada: as fatias e a
/// frente sao a mesma imagem, projetada em perspectiva AQUI (em Dart),
/// e desenhada como malha de triangulos com textura. Nao depende do
/// compositor aplicar matriz 3D: em alguns aparelhos a perspectiva do
/// Transform e achatada e todas as fatias caem em cima da frente.
///
/// A frente NAO e escurecida; as fatias vao de 42% a 64% do brilho, a
/// mais funda mais escura, como a lateral de um solido na sombra.
class ExtrudeSnapshotPainter extends SnapshotPainter {
  ExtrudeSnapshotPainter({
    required this.perspective,
    required this.transform2d,
    required this.opacity,
    required this.passos,
    required this.passo,
  });

  /// Perspectiva x Rz x Ry x Rx, aplicada em torno do centro.
  final Matrix4 perspective;

  /// Transform 2D da camada (pivot, skew, escala), tambem no centro.
  final Matrix4 transform2d;

  final double opacity;
  final int passos;
  final double passo;

  /// Malha por fatia: quanto mais fina, mais fiel a curva da
  /// perspectiva (dois triangulos so entortam o texto).
  static const int _grade = 6;

  /// Matriz completa da fatia (recuo [z]) em torno do centro da caixa.
  Matrix4 _matriz(Size size, double z) {
    final cx = size.width / 2, cy = size.height / 2;
    return Matrix4.identity()
      ..translateByDouble(cx, cy, 0, 1)
      ..multiply(perspective)
      ..translateByDouble(0, 0, z, 1)
      ..translateByDouble(-cx, -cy, 0, 1)
      ..translateByDouble(cx, cy, 0, 1)
      ..multiply(transform2d)
      ..translateByDouble(-cx, -cy, 0, 1);
  }

  @override
  void paint(PaintingContext context, Offset offset, Size size,
      PaintingContextCallback painter) {
    // Sem foto (nao deveria acontecer no modo forcado): so a frente,
    // pelo caminho normal de transform.
    context.pushTransform(true, offset, _matriz(size, 0), (ctx, off) {
      ctx.pushOpacity(off, (opacity * 255).round().clamp(0, 255),
          (c2, o2) => painter(c2, o2));
    });
  }

  @override
  void paintSnapshot(PaintingContext context, Offset offset, Size size,
      ui.Image image, Size sourceSize, double pixelRatio) {
    final canvas = context.canvas;
    final alpha = opacity.clamp(0.0, 1.0);
    final shader = ui.ImageShader(
        image, TileMode.clamp, TileMode.clamp, Matrix4.identity().storage);
    final paint = Paint()
      ..shader = shader
      ..filterQuality = FilterQuality.medium;
    final iw = image.width.toDouble(), ih = image.height.toDouble();
    const g = _grade;
    final n = (g + 1) * (g + 1);
    final pos = Float32List(n * 2);
    final uv = Float32List(n * 2);
    final cores = Int32List(n);
    final idx = Uint16List(g * g * 6);
    var k = 0;
    for (var j = 0; j < g; j++) {
      for (var i = 0; i < g; i++) {
        final a = j * (g + 1) + i;
        final b = a + 1;
        final c = a + (g + 1);
        final d = c + 1;
        idx[k++] = a;
        idx[k++] = b;
        idx[k++] = c;
        idx[k++] = b;
        idx[k++] = d;
        idx[k++] = c;
      }
    }

    void desenha(double z, double brilho) {
      final st = _matriz(size, z).storage;
      final cor = Color.fromRGBO(
        (brilho * 255).round(),
        (brilho * 255).round(),
        (brilho * 255).round(),
        alpha,
      ).toARGB32();
      var p = 0;
      for (var j = 0; j <= g; j++) {
        for (var i = 0; i <= g; i++) {
          final u = i / g, v = j / g;
          // Ponto (x, y, 0, 1) pela matriz (coluna-maior) com divisao
          // pela perspectiva.
          final x = u * size.width, y = v * size.height;
          final tx = st[0] * x + st[4] * y + st[12];
          final ty = st[1] * x + st[5] * y + st[13];
          var tw = st[3] * x + st[7] * y + st[15];
          if (tw.abs() < 1e-6) tw = 1e-6;
          pos[p * 2] = offset.dx + tx / tw;
          pos[p * 2 + 1] = offset.dy + ty / tw;
          uv[p * 2] = u * iw;
          uv[p * 2 + 1] = v * ih;
          cores[p] = cor;
          p++;
        }
      }
      canvas.drawVertices(
        ui.Vertices.raw(ui.VertexMode.triangles, pos,
            textureCoordinates: uv, colors: cores, indices: idx),
        BlendMode.modulate,
        paint,
      );
    }

    for (var i = passos; i >= 1; i--) {
      desenha(i * passo, 0.42 + 0.22 * (1 - i / passos));
    }
    desenha(0, 1);
  }

  @override
  bool shouldRepaint(covariant ExtrudeSnapshotPainter old) =>
      old.perspective != perspective ||
      old.transform2d != transform2d ||
      old.opacity != opacity ||
      old.passos != passos ||
      old.passo != passo;
}
