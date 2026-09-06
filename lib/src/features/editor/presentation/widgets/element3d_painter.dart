import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../../application/texture_cache.dart';
import '../../domain/element3d.dart';
import '../../domain/layer.dart';

/// Pintor de Elemento 3D: rotaciona os VERTICES no espaco (X depois Y —
/// a mesma ordem da matematica de orbita do app), projeta com a focal
/// padrao (1200) e desenha as faces do fundo para a frente (algoritmo do
/// pintor) com sombreamento lambertiano. O Rz e aplicado pelo canvas,
/// como nas particulas — girar em torno do eixo de visao equivale a
/// girar a imagem projetada.
class Element3DPainter extends CustomPainter {
  Element3DPainter({
    required this.layer,
    this.rotXDeg = 0,
    this.rotYDeg = 0,
  });

  final Element3DLayer layer;
  final double rotXDeg;
  final double rotYDeg;

  static const double _focal = 1200;

  @override
  void paint(Canvas canvas, Size size) {
    final mesh = element3DMesh(layer.kind);
    final cx = size.width / 2;
    final cy = size.height / 2;
    final s = layer.size;
    final rx = rotXDeg * math.pi / 180;
    final ry = rotYDeg * math.pi / 180;
    final cxr = math.cos(rx), sxr = math.sin(rx);
    final cyr = math.cos(ry), syr = math.sin(ry);

    // Rotaciona e escala todos os vertices uma vez.
    final n = mesh.verts.length;
    final wx = List<double>.filled(n, 0);
    final wy = List<double>.filled(n, 0);
    final wz = List<double>.filled(n, 0);
    for (var i = 0; i < n; i++) {
      final v = mesh.verts[i];
      final vx = v[0] * s;
      var vy = v[1] * s;
      var vz = v[2] * s;
      final y1 = vy * cxr - vz * sxr;
      final z1 = vy * sxr + vz * cxr;
      final x1 = vx * cyr + z1 * syr;
      final z2 = -vx * syr + z1 * cyr;
      wx[i] = x1;
      wy[i] = y1;
      wz[i] = z2;
    }

    // Luz fixa vinda de cima/esquerda/frente (normalizada).
    const lx = -0.37, ly = -0.55, lz = -0.75;

    final order = <(double, int)>[];
    for (var f = 0; f < mesh.faces.length; f++) {
      var depth = 0.0;
      for (final i in mesh.faces[f]) {
        depth += wz[i];
      }
      order.add((depth / mesh.faces[f].length, f));
    }
    // Fundo primeiro: z maior = mais longe (persp = f/(f+z)).
    order.sort((a, b) => b.$1.compareTo(a.$1));

    final fill = Paint();
    final stroke = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.4
      ..strokeJoin = StrokeJoin.round;
    final base = layer.color;
    final edgeColor = Color.lerp(base, Colors.black, 0.55)!;

    // IMAGEM: chega do cache; se ainda nao chegou, a face sai lisa e o
    // cache avisa quando carregar.
    final imgPath = layer.imagePath;
    final img = imgPath == null ? null : TextureCache.instance.imageFor(imgPath);
    var lminX = double.infinity, lminY = double.infinity, lminZ = double.infinity;
    var lmaxX = -double.infinity, lmaxY = -double.infinity, lmaxZ = -double.infinity;
    if (img != null) {
      for (final v in mesh.verts) {
        if (v[0] < lminX) lminX = v[0];
        if (v[0] > lmaxX) lmaxX = v[0];
        if (v[1] < lminY) lminY = v[1];
        if (v[1] > lmaxY) lmaxY = v[1];
        if (v[2] < lminZ) lminZ = v[2];
        if (v[2] > lmaxZ) lmaxZ = v[2];
      }
    }
    double faixa(double a, double lo, double hi) =>
        hi - lo < 1e-9 ? 0.5 : ((a - lo) / (hi - lo)).clamp(0.0, 1.0);
    ui.ImageShader? shader;
    if (img != null) {
      shader = ui.ImageShader(
          img, TileMode.clamp, TileMode.clamp, Matrix4.identity().storage);
    }

    for (final (_, f) in order) {
      final face = mesh.faces[f];
      // Normal por Newell (robusto para poligonos de qualquer lado) — em
      // mundo (girada) e em local (para o eixo da imagem).
      var nx = 0.0, ny = 0.0, nz = 0.0;
      var lnx = 0.0, lny = 0.0, lnz = 0.0;
      for (var i = 0; i < face.length; i++) {
        final a = face[i];
        final b = face[(i + 1) % face.length];
        nx += (wy[a] - wy[b]) * (wz[a] + wz[b]);
        ny += (wz[a] - wz[b]) * (wx[a] + wx[b]);
        nz += (wx[a] - wx[b]) * (wy[a] + wy[b]);
        if (img != null) {
          final va = mesh.verts[a], vb = mesh.verts[b];
          lnx += (va[1] - vb[1]) * (va[2] + vb[2]);
          lny += (va[2] - vb[2]) * (va[0] + vb[0]);
          lnz += (va[0] - vb[0]) * (va[1] + vb[1]);
        }
      }
      final len = math.sqrt(nx * nx + ny * ny + nz * nz);
      double shade = 0.65;
      var nnx = 0.0, nny = 0.0, nnz = 0.0;
      if (len > 1e-9) {
        nnx = nx / len;
        nny = ny / len;
        nnz = nz / len;
        final dot = nnx * lx + nny * ly + nnz * lz;
        shade = 0.34 + 0.66 * dot.abs();
      }

      // REFLEXO DO AMBIENTE: a direcao espelhada da vista, olhada no
      // mapa do ambiente; Fresnel faz a borda refletir mais que o meio.
      // A camera esta em -Z, entao a vista da superficie e (0, 0, -1); o
      // mapa tem Y para cima e a malha tem Y para baixo, por isso o -ry.
      Color? reflexo;
      var amount = 0.0;
      if (layer.reflect > 0 && len > 1e-9) {
        // A face virada para tras tambem e desenhada (nao ha descarte
        // aqui): a normal virada para a camera e a que conta.
        final sinal = nnz < 0 ? 1.0 : -1.0;
        final fx = nnx * sinal, fy = nny * sinal, fz = nnz * sinal;
        final nv = (-fz).clamp(0.0, 1.0);
        final rx = 2 * nv * fx, ry = 2 * nv * fy, rz = 2 * nv * fz + 1;
        final (er, eg, eb) = environmentColor(
          layer.environment,
          rx,
          -ry,
          rz,
          sunX: lx,
          sunY: -ly,
          sunZ: lz,
          sunSharp: 90,
          sunGain: 0.9,
        );
        final fresnel = 0.04 + 0.96 * math.pow(1 - nv, 5).toDouble();
        amount = (layer.reflect * (0.25 + 0.75 * fresnel)).clamp(0.0, 1.0);
        reflexo = Color.from(
          alpha: 1,
          red: er.clamp(0.0, 1.0),
          green: eg.clamp(0.0, 1.0),
          blue: eb.clamp(0.0, 1.0),
        );
      }

      final path = Path();
      final pontos = <Offset>[];
      for (var i = 0; i < face.length; i++) {
        final v = face[i];
        final persp = _focal / (_focal + wz[v]).clamp(60.0, double.infinity);
        final px = cx + wx[v] * persp;
        final py = cy + wy[v] * persp;
        pontos.add(Offset(px, py));
        if (i == 0) {
          path.moveTo(px, py);
        } else {
          path.lineTo(px, py);
        }
      }
      path.close();

      if (img != null && shader != null && face.length >= 3) {
        // Leque de triangulos com coordenadas de imagem por projecao de
        // caixa; a luz entra como cor por vertice, multiplicada.
        final ax = lnx.abs(), ay = lny.abs(), az = lnz.abs();
        Offset uv(int i) {
          final v = mesh.verts[i];
          if (ax >= ay && ax >= az) {
            return Offset(faixa(v[2], lminZ, lmaxZ), faixa(v[1], lminY, lmaxY));
          }
          if (ay >= ax && ay >= az) {
            return Offset(faixa(v[0], lminX, lmaxX), faixa(v[2], lminZ, lmaxZ));
          }
          return Offset(faixa(v[0], lminX, lmaxX), faixa(v[1], lminY, lmaxY));
        }
        final n = face.length - 2;
        final positions = Float32List(n * 6);
        final coords = Float32List(n * 6);
        final colors = Int32List(n * 3);
        final luz = Color.lerp(Colors.black, Colors.white, shade)!.toARGB32();
        final w = img.width.toDouble(), h = img.height.toDouble();
        for (var k = 0; k < n; k++) {
          final ids = [face[0], face[k + 1], face[k + 2]];
          final ps = [pontos[0], pontos[k + 1], pontos[k + 2]];
          for (var m = 0; m < 3; m++) {
            positions[k * 6 + m * 2] = ps[m].dx;
            positions[k * 6 + m * 2 + 1] = ps[m].dy;
            final t = uv(ids[m]);
            coords[k * 6 + m * 2] = t.dx * w;
            coords[k * 6 + m * 2 + 1] = t.dy * h;
            colors[k * 3 + m] = luz;
          }
        }
        canvas.drawVertices(
          ui.Vertices.raw(ui.VertexMode.triangles, positions,
              textureCoordinates: coords, colors: colors),
          BlendMode.modulate,
          Paint()..shader = shader,
        );
      } else {
        fill.color = Color.lerp(Colors.black, base, shade)!;
        canvas.drawPath(path, fill);
      }
      if (reflexo != null && amount > 0.002) {
        canvas.drawPath(
            path, Paint()..color = reflexo.withValues(alpha: amount));
      }
      if (layer.edges) {
        stroke.color = edgeColor;
        canvas.drawPath(path, stroke);
      }
    }
  }

  @override
  bool shouldRepaint(Element3DPainter old) =>
      old.layer != layer ||
      old.rotXDeg != rotXDeg ||
      old.rotYDeg != rotYDeg;
}
