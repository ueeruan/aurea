import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../../application/mesh_cache.dart';
import '../../application/texture_cache.dart';
import '../../domain/element3d.dart';
import '../../domain/layer.dart';

/// Cores do degrade "brilhante" padrao: roxo -> azul -> rosa, como nas
/// referencias de motion 3D.
const kGlossyGradientDefault = <Color>[
  Color(0xFF7A3FF2),
  Color(0xFF2F7BFF),
  Color(0xFFFF4FD8),
];

/// Um solido colocado no MUNDO da composicao: onde esta (centro em
/// coordenadas da composicao, profundidade Z), como esta girado e
/// escalado, e com que material aparece.
class World3DItem {
  const World3DItem({
    required this.layer,
    required this.center,
    this.z = 0,
    this.scaleX = 1,
    this.scaleY = 1,
    this.rotXDeg = 0,
    this.rotYDeg = 0,
    this.rotZDeg = 0,
    this.opacity = 1,
    this.selected = false,
    this._material,
    this._gradient,
    this._shininess,
  });

  final Element3DLayer layer;
  final Offset center;
  final double z;
  final double scaleX;
  final double scaleY;
  final double rotXDeg;
  final double rotYDeg;
  final double rotZDeg;
  final double opacity;
  final bool selected;

  final int? _material;
  final List<Color>? _gradient;
  final double? _shininess;

  /// 0 solido, 1 brilhante (degrade), 2 vidro, 3 metal, 4 fosco. Sem
  /// valor proprio, vem da camada.
  int get material => _material ?? layer.material;
  List<Color> get gradient => _gradient ?? layer.gradient;
  double get shininess => _shininess ?? layer.shininess;
}

/// Um triangulo pronto para pintar: profundidade media (mundo), pontos
/// projetados, cor por vertice (Gouraud), quais arestas sao borda do
/// poligono original (bits 0..2) e, se houver imagem, as coordenadas.
class World3DTri {
  World3DTri({
    required this.depth,
    required this.pts,
    required this.colors,
    required this.item,
    this.edges = 0,
    this.uv,
  });

  final double depth;
  final List<Offset> pts;
  final List<Color> colors;
  final int item;
  final int edges;
  final List<Offset>? uv;
}

/// Normais suaves por (face, vertice) com angulo de vinco: em faces
/// vizinhas quase coplanares o vertice compartilha a normal (esfera
/// lisa); em quinas (cubo) cada face fica com a sua (arestas nitidas).
class _MeshInfo {
  _MeshInfo(this.faceNormals, this.smooth);

  final List<List<double>> faceNormals;
  final List<List<List<double>>> smooth;
}

final Map<Object, _MeshInfo> _infoCache = {};

/// Chave: o tipo do solido nativo, ou o caminho do modelo importado.
_MeshInfo _infoDe(Object key, Element3DMesh mesh) {
  final pronto = _infoCache[key];
  if (pronto != null) return pronto;
  if (_infoCache.length > 64) _infoCache.clear();
  return _infoCache[key] = _buildInfo(mesh);
}

_MeshInfo _buildInfo(Element3DMesh mesh) {
  final normals = <List<double>>[];
  for (final face in mesh.faces) {
    var nx = 0.0, ny = 0.0, nz = 0.0;
    for (var i = 0; i < face.length; i++) {
      final a = mesh.verts[face[i]];
      final b = mesh.verts[face[(i + 1) % face.length]];
      nx += (a[1] - b[1]) * (a[2] + b[2]);
      ny += (a[2] - b[2]) * (a[0] + b[0]);
      nz += (a[0] - b[0]) * (a[1] + b[1]);
    }
    final len = math.sqrt(nx * nx + ny * ny + nz * nz);
    normals.add(len < 1e-9 ? [0, 0, -1] : [nx / len, ny / len, nz / len]);
  }
  final adj = <int, List<int>>{};
  for (var f = 0; f < mesh.faces.length; f++) {
    for (final v in mesh.faces[f]) {
      (adj[v] ??= []).add(f);
    }
  }
  const cosVinco = 0.8; // ~37 graus: o chanfro de 45 fica nitido
  final smooth = <List<List<double>>>[];
  for (var f = 0; f < mesh.faces.length; f++) {
    final n0 = normals[f];
    final porVertice = <List<double>>[];
    for (final v in mesh.faces[f]) {
      var sx = 0.0, sy = 0.0, sz = 0.0;
      for (final g in adj[v]!) {
        final n = normals[g];
        final d = n[0] * n0[0] + n[1] * n0[1] + n[2] * n0[2];
        if (d >= cosVinco) {
          sx += n[0];
          sy += n[1];
          sz += n[2];
        }
      }
      final len = math.sqrt(sx * sx + sy * sy + sz * sz);
      porVertice.add(len < 1e-9 ? n0 : [sx / len, sy / len, sz / len]);
    }
    smooth.add(porVertice);
  }
  return _MeshInfo(normals, smooth);
}

/// MUNDO 3D: todos os solidos de uma fileira de camadas 3D vizinhas
/// pintados numa unica cena, com os triangulos de TODOS ordenados por
/// profundidade — um cubo entra dentro do outro, uma esfera passa por
/// tras de um prisma, o vidro deixa ver o que esta atras. Cada face e
/// subdividida em triangulos pequenos para que a ordenacao (algoritmo do
/// pintor) funcione tambem quando os solidos se atravessam.
///
/// Projecao unica: camera no centro da composicao, focal 1200 (a mesma
/// do resto do motor). Sombreamento Gouraud por vertice com angulo de
/// vinco, materiais (solido, brilhante em degrade, vidro, metal, fosco),
/// reflexo do ambiente e imagem nas faces.
class World3DPainter extends CustomPainter {
  World3DPainter({required this.items, this.focal = 1200});

  final List<World3DItem> items;
  final double focal;

  /// Monta a lista de triangulos (ainda sem ordenar). Publico para os
  /// testes: a ordem por profundidade e o que garante a interpenetracao.
  static List<World3DTri> build(List<World3DItem> items, Size size,
      {double focal = 1200, List<ui.Image?>? images}) {
    final cx = size.width / 2, cy = size.height / 2;
    final out = <World3DTri>[];
    for (var k = 0; k < items.length; k++) {
      final it = items[k];
      final l = it.layer;
      // Modelo importado, se ja chegou; senao o solido nativo.
      final custom =
          l.meshPath == null ? null : MeshCache.instance.meshFor(l.meshPath!);
      final mesh = custom ?? element3DMesh(l.kind);
      final info = _infoDe(custom == null ? l.kind : l.meshPath!, mesh);
      final img = images == null || k >= images.length ? null : images[k];

      final rx = it.rotXDeg * math.pi / 180;
      final ry = it.rotYDeg * math.pi / 180;
      final rz = it.rotZDeg * math.pi / 180;
      final cxr = math.cos(rx), sxr = math.sin(rx);
      final cyr = math.cos(ry), syr = math.sin(ry);
      final czr = math.cos(rz), szr = math.sin(rz);
      (double, double, double) rot(double x, double y, double z) {
        final y1 = y * cxr - z * sxr;
        final z1 = y * sxr + z * cxr;
        final x1 = x * cyr + z1 * syr;
        final z2 = -x * syr + z1 * cyr;
        return (x1 * czr - y1 * szr, x1 * szr + y1 * czr, z2);
      }

      final s = l.size;
      final sx = it.scaleX, sy = it.scaleY;
      final sz = (sx.abs() + sy.abs()) / 2;
      final ox = it.center.dx - cx, oy = it.center.dy - cy, oz = it.z;

      final n = mesh.verts.length;
      final wx = Float64List(n), wy = Float64List(n), wz = Float64List(n);
      var lminX = double.infinity, lminY = double.infinity;
      var lminZ = double.infinity;
      var lmaxX = -double.infinity, lmaxY = -double.infinity;
      var lmaxZ = -double.infinity;
      for (var i = 0; i < n; i++) {
        final v = mesh.verts[i];
        final (x, y, z) = rot(v[0] * s * sx, v[1] * s * sy, v[2] * s * sz);
        wx[i] = x + ox;
        wy[i] = y + oy;
        wz[i] = z + oz;
        if (v[0] < lminX) lminX = v[0];
        if (v[0] > lmaxX) lmaxX = v[0];
        if (v[1] < lminY) lminY = v[1];
        if (v[1] > lmaxY) lmaxY = v[1];
        if (v[2] < lminZ) lminZ = v[2];
        if (v[2] > lmaxZ) lmaxZ = v[2];
      }
      double faixa(double a, double lo, double hi) =>
          hi - lo < 1e-9 ? 0.5 : ((a - lo) / (hi - lo)).clamp(0.0, 1.0);

      Offset projeta(double x, double y, double z) {
        final persp = focal / math.max(60.0, focal + z);
        return Offset(cx + x * persp, cy + y * persp);
      }

      // Subdivisao: triangulos grandes viram quatro, ate duas vezes, para
      // a ordenacao por profundidade acertar quando solidos se cruzam.
      // Modelos importados grandes ja vem em triangulos pequenos.
      final limiar =
          items.length > 1 && mesh.faces.length <= 2000 ? 70.0 : 1e9;
      void emite(List<List<double>> v, int mask, int prof) {
        double dist(List<double> a, List<double> b) {
          final dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
          return math.sqrt(dx * dx + dy * dy + dz * dz);
        }

        final maior = math.max(
            dist(v[0], v[1]), math.max(dist(v[1], v[2]), dist(v[2], v[0])));
        if (prof < 2 && maior > limiar) {
          List<double> meio(List<double> a, List<double> b) => [
                for (var i = 0; i < a.length; i++) (a[i] + b[i]) / 2,
              ];
          final m01 = meio(v[0], v[1]);
          final m12 = meio(v[1], v[2]);
          final m20 = meio(v[2], v[0]);
          emite([v[0], m01, m20], mask & 5, prof + 1);
          emite([m01, v[1], m12], mask & 3, prof + 1);
          emite([m20, m12, v[2]], mask & 6, prof + 1);
          emite([m01, m12, m20], 0, prof + 1);
          return;
        }
        final pts = <Offset>[];
        final cores = <Color>[];
        final uv = img == null ? null : <Offset>[];
        var depth = 0.0;
        for (final p in v) {
          pts.add(projeta(p[0], p[1], p[2]));
          depth += p[2];
          cores.add(Color.from(
              alpha: p[3].clamp(0.0, 1.0),
              red: p[4].clamp(0.0, 1.0),
              green: p[5].clamp(0.0, 1.0),
              blue: p[6].clamp(0.0, 1.0)));
          uv?.add(Offset(p[7], p[8]));
        }
        out.add(World3DTri(
          depth: depth / 3,
          pts: pts,
          colors: cores,
          item: k,
          edges: mask,
          uv: uv,
        ));
      }

      for (var f = 0; f < mesh.faces.length; f++) {
        final face = mesh.faces[f];
        if (face.length < 3) continue;
        final ln = info.faceNormals[f];
        final ax = ln[0].abs(), ay = ln[1].abs(), az = ln[2].abs();
        // Vertices da face: mundo + cor + uv, num vetor so.
        final vs = <List<double>>[];
        for (var i = 0; i < face.length; i++) {
          final vi = face[i];
          final sn = info.smooth[f][i];
          final (nx, ny, nz) = rot(sn[0], sn[1], sn[2]);
          final cor = _shade(it, nx, ny, nz, textured: img != null);
          final lv = mesh.verts[vi];
          double u = 0, w = 0;
          if (img != null) {
            if (ax >= ay && ax >= az) {
              u = faixa(lv[2], lminZ, lmaxZ);
              w = faixa(lv[1], lminY, lmaxY);
            } else if (ay >= ax && ay >= az) {
              u = faixa(lv[0], lminX, lmaxX);
              w = faixa(lv[2], lminZ, lmaxZ);
            } else {
              u = faixa(lv[0], lminX, lmaxX);
              w = faixa(lv[1], lminY, lmaxY);
            }
          }
          vs.add([wx[vi], wy[vi], wz[vi], cor.a, cor.r, cor.g, cor.b, u, w]);
        }
        final nf = face.length - 2;
        for (var t = 0; t < nf; t++) {
          final mask = (t == 0 ? 1 : 0) | 2 | (t == nf - 1 ? 4 : 0);
          emite([vs[0], vs[t + 1], vs[t + 2]], mask, 0);
        }
      }
    }
    return out;
  }

  /// Cor final de um vertice: material + luz + reflexo, ja composta.
  static Color _shade(World3DItem it, double nx, double ny, double nz,
      {bool textured = false}) {
    // A normal virada para a camera e a que conta (faces de tras tambem
    // sao pintadas, sem descarte).
    if (nz > 0) {
      nx = -nx;
      ny = -ny;
      nz = -nz;
    }
    final nv = (-nz).clamp(0.0, 1.0);
    const lx = -0.37, ly = -0.55, lz = -0.75;
    final dot = (nx * lx + ny * ly + nz * lz).clamp(-1.0, 1.0);
    final diffuse = 0.34 + 0.66 * dot.abs();
    // Blinn-Phong: H = normalize(L + V), V = (0, 0, -1).
    const hx = -0.37, hy = -0.55, hz = -1.75;
    final hl = math.sqrt(hx * hx + hy * hy + hz * hz);
    final ndoth = ((nx * hx + ny * hy + nz * hz) / hl).clamp(0.0, 1.0);
    final spec =
        math.pow(ndoth, 6 + 110 * it.shininess.clamp(0.0, 1.0)).toDouble();
    final fresnel = 0.04 + 0.96 * math.pow(1 - nv, 5).toDouble();
    final base = textured ? Colors.white : it.layer.color;

    (double, double, double) ambiente() {
      final rxr = 2 * nv * nx, ryr = 2 * nv * ny, rzr = 2 * nv * nz + 1;
      return environmentColor(
        it.layer.environment,
        rxr,
        -ryr,
        rzr,
        sunX: lx,
        sunY: -ly,
        sunZ: lz,
        sunSharp: 90,
        sunGain: 0.9,
      );
    }

    Color deAmbiente() {
      final (er, eg, eb) = ambiente();
      return Color.from(
          alpha: 1,
          red: er.clamp(0.0, 1.0),
          green: eg.clamp(0.0, 1.0),
          blue: eb.clamp(0.0, 1.0));
    }

    Color fill;
    var alpha = 1.0;
    Color? over;
    var amt = 0.0;
    switch (textured ? 0 : it.material) {
      case 1: // brilhante em degrade (iridescente)
        final t = (0.5 - ny * 0.45 + nx * 0.2 + (1 - nv) * 0.25)
            .clamp(0.0, 1.0);
        final col = gradientAt(it.gradient, t);
        fill = Color.lerp(Colors.black, col, 0.55 + 0.45 * diffuse)!;
        over = Colors.white;
        amt = (spec * 0.9 + fresnel * 0.3).clamp(0.0, 1.0);
      case 2: // vidro
        fill = Color.lerp(base, Colors.white, 0.55)!;
        alpha = (0.14 + 0.6 * fresnel).clamp(0.0, 1.0);
        over = it.layer.reflect > 0
            ? Color.lerp(deAmbiente(), Colors.white, spec)!
            : Colors.white;
        amt = (spec * 0.95 + fresnel * 0.25 + it.layer.reflect * 0.2)
            .clamp(0.0, 1.0);
      case 3: // metal
        fill = Color.lerp(Colors.black, base, 0.25 + 0.5 * diffuse)!;
        over = Color.lerp(deAmbiente(), Colors.white, spec * 0.8)!;
        amt = (0.75 * (0.5 + 0.5 * fresnel) + spec * 0.2).clamp(0.0, 1.0);
      case 4: // fosco
        fill = Color.lerp(Colors.black, base, 0.5 + 0.5 * diffuse)!;
      default: // solido + reflexo do ambiente se ligado
        fill = Color.lerp(Colors.black, base, diffuse)!;
        if (it.layer.reflect > 0) {
          over = deAmbiente();
          amt = (it.layer.reflect * (0.25 + 0.75 * fresnel)).clamp(0.0, 1.0);
        }
    }
    final c = over == null || amt <= 0.002 ? fill : Color.lerp(fill, over, amt)!;
    return c.withValues(alpha: (alpha * it.opacity).clamp(0.0, 1.0));
  }

  /// Cor de um degrade de varias paradas em t (0..1).
  static Color gradientAt(List<Color> cores, double t) {
    if (cores.isEmpty) return Colors.white;
    if (cores.length == 1) return cores.first;
    final x = t.clamp(0.0, 1.0) * (cores.length - 1);
    final i = x.floor().clamp(0, cores.length - 2);
    return Color.lerp(cores[i], cores[i + 1], x - i)!;
  }

  @override
  void paint(Canvas canvas, Size size) {
    final images = <ui.Image?>[
      for (final it in items)
        it.layer.imagePath == null
            ? null
            : TextureCache.instance.imageFor(it.layer.imagePath!),
    ];
    final tris = build(items, size, focal: focal, images: images);
    // Fundo primeiro: z maior = mais longe.
    tris.sort((a, b) => b.depth.compareTo(a.depth));

    final fill = Paint();
    final stroke = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.4
      ..strokeJoin = StrokeJoin.round
      ..strokeCap = StrokeCap.round;
    final shaders = <int, ui.ImageShader?>{};
    final edgeColors = <int, Color>{};
    for (final t in tris) {
      final it = items[t.item];
      final img = images[t.item];
      // Alarga 0.3px a partir do centro para nao aparecer a emenda entre
      // triangulos vizinhos (anti-aliasing dos dois lados). No vidro
      // (translucido) a sobreposicao acumularia alpha e desenharia a
      // malha: ali vai sem alargar e sem anti-aliasing.
      final translucido = t.colors.any((c) => c.a < 0.999);
      final c = (t.pts[0] + t.pts[1] + t.pts[2]) / 3;
      final pts = translucido
          ? t.pts
          : [
              for (final p in t.pts)
                () {
                  final d = p - c;
                  final len = d.distance;
                  return len < 1e-6 ? p : p + d / len * 0.3;
                }(),
            ];
      fill.isAntiAlias = !translucido;
      if (img != null && t.uv != null) {
        final shader = shaders[t.item] ??= ui.ImageShader(
            img, TileMode.clamp, TileMode.clamp, Matrix4.identity().storage);
        final w = img.width.toDouble(), h = img.height.toDouble();
        final positions = Float32List(6);
        final coords = Float32List(6);
        final colors = Int32List(3);
        for (var m = 0; m < 3; m++) {
          positions[m * 2] = pts[m].dx;
          positions[m * 2 + 1] = pts[m].dy;
          coords[m * 2] = t.uv![m].dx * w;
          coords[m * 2 + 1] = t.uv![m].dy * h;
          colors[m] = t.colors[m].toARGB32();
        }
        canvas.drawVertices(
          ui.Vertices.raw(ui.VertexMode.triangles, positions,
              textureCoordinates: coords, colors: colors),
          BlendMode.modulate,
          Paint()..shader = shader,
        );
      } else {
        final c0 = t.colors[0], c1 = t.colors[1], c2 = t.colors[2];
        final plano = c0 == c1 && c1 == c2;
        if (plano) {
          fill.color = c0;
          canvas.drawPath(
              Path()
                ..moveTo(pts[0].dx, pts[0].dy)
                ..lineTo(pts[1].dx, pts[1].dy)
                ..lineTo(pts[2].dx, pts[2].dy)
                ..close(),
              fill);
        } else {
          final positions = Float32List.fromList([
            pts[0].dx, pts[0].dy, pts[1].dx, pts[1].dy, pts[2].dx, pts[2].dy,
          ]);
          final colors = Int32List.fromList(
              [c0.toARGB32(), c1.toARGB32(), c2.toARGB32()]);
          canvas.drawVertices(
            ui.Vertices.raw(ui.VertexMode.triangles, positions,
                colors: colors),
            BlendMode.srcOver,
            fill..color = Colors.white,
          );
        }
      }
      if (it.layer.edges && t.edges != 0) {
        final ec = edgeColors[t.item] ??=
            Color.lerp(it.layer.color, Colors.black, 0.55)!
                .withValues(alpha: it.opacity.clamp(0.0, 1.0));
        stroke.color = ec;
        if (t.edges & 1 != 0) canvas.drawLine(t.pts[0], t.pts[1], stroke);
        if (t.edges & 2 != 0) canvas.drawLine(t.pts[1], t.pts[2], stroke);
        if (t.edges & 4 != 0) canvas.drawLine(t.pts[2], t.pts[0], stroke);
      }
    }

    // Selecao: caixa branca em volta do solido selecionado (projetado).
    for (var k = 0; k < items.length; k++) {
      if (!items[k].selected) continue;
      var r = Rect.zero;
      var first = true;
      for (final t in tris) {
        if (t.item != k) continue;
        for (final p in t.pts) {
          final pr = Rect.fromLTWH(p.dx, p.dy, 0, 0);
          r = first ? pr : r.expandToInclude(pr);
          first = false;
        }
      }
      if (first) continue;
      canvas.drawRect(
          r.inflate(6),
          Paint()
            ..style = PaintingStyle.stroke
            ..strokeWidth = 4
            ..color = Colors.white);
    }
  }

  @override
  bool shouldRepaint(World3DPainter old) {
    if (old.focal != focal || old.items.length != items.length) return true;
    for (var i = 0; i < items.length; i++) {
      final a = old.items[i], b = items[i];
      if (a.layer != b.layer ||
          a.center != b.center ||
          a.z != b.z ||
          a.scaleX != b.scaleX ||
          a.scaleY != b.scaleY ||
          a.rotXDeg != b.rotXDeg ||
          a.rotYDeg != b.rotYDeg ||
          a.rotZDeg != b.rotZDeg ||
          a.opacity != b.opacity ||
          a.selected != b.selected ||
          a.material != b.material ||
          a.shininess != b.shininess ||
          !identical(a.gradient, b.gradient)) {
        return true;
      }
    }
    return false;
  }
}
