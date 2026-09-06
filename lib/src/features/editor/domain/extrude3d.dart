import 'dart:math' as math;
import 'dart:ui';

import 'element3d.dart';
import 'mask.dart';

/// EXTRUSAO: uma forma plana vira volume.
///
/// E o que separa "logo com sombrinha" de logo de verdade girando na
/// tela. A forma ja existe no aplicativo (as mesmas curvas da mascara e
/// da camada de forma); o que faltava era a espessura, as paredes
/// laterais e as duas tampas.
///
/// A tampa e o problema de verdade: um poligono qualquer nao e um
/// triangulo, e a placa de video so desenha triangulo. A saida classica
/// e o CORTE DE ORELHA — achar um canto que nao esconde ninguem, cortar,
/// repetir. Funciona em forma concava (um "C", um "S"), que e onde a
/// abordagem ingenua de leque falha feio.

/// Area com sinal: positiva = sentido anti-horario na convencao de
/// tela (y para baixo).
double signedArea(List<Offset> pts) {
  var soma = 0.0;
  for (var i = 0; i < pts.length; i++) {
    final a = pts[i];
    final b = pts[(i + 1) % pts.length];
    soma += a.dx * b.dy - b.dx * a.dy;
  }
  return soma / 2;
}

bool _dentro(Offset p, Offset a, Offset b, Offset c) {
  double lado(Offset u, Offset v, Offset w) =>
      (v.dx - u.dx) * (w.dy - u.dy) - (v.dy - u.dy) * (w.dx - u.dx);
  final d1 = lado(a, b, p);
  final d2 = lado(b, c, p);
  final d3 = lado(c, a, p);
  final neg = d1 < 0 || d2 < 0 || d3 < 0;
  final pos = d1 > 0 || d2 > 0 || d3 > 0;
  return !(neg && pos);
}

/// CORTE DE ORELHA: parte um poligono em triangulos.
///
/// Devolve trincas de indices no proprio [pts]. Poligono com menos de
/// tres pontos, ou degenerado, devolve lista vazia — nunca triangulo
/// invalido, que na hora de desenhar viraria buraco preto.
List<List<int>> earClip(List<Offset> pts) {
  if (pts.length < 3) return const [];

  final indices = [for (var i = 0; i < pts.length; i++) i];
  // Trabalha sempre no mesmo sentido: assim "convexo" tem um sinal so.
  if (signedArea(pts) < 0) {
    final invertido = indices.reversed.toList();
    indices
      ..clear()
      ..addAll(invertido);
  }

  final out = <List<int>>[];
  var guarda = pts.length * pts.length + 16;

  while (indices.length > 3 && guarda-- > 0) {
    var cortou = false;
    for (var i = 0; i < indices.length; i++) {
      final ia = indices[(i - 1 + indices.length) % indices.length];
      final ib = indices[i];
      final ic = indices[(i + 1) % indices.length];
      final a = pts[ia], b = pts[ib], c = pts[ic];

      // Canto reflexo nao e orelha.
      final cruz = (b.dx - a.dx) * (c.dy - a.dy) -
          (b.dy - a.dy) * (c.dx - a.dx);
      if (cruz <= 0) continue;

      // Nem orelha que engole outro vertice.
      var limpo = true;
      for (final j in indices) {
        if (j == ia || j == ib || j == ic) continue;
        if (_dentro(pts[j], a, b, c)) {
          limpo = false;
          break;
        }
      }
      if (!limpo) continue;

      out.add([ia, ib, ic]);
      indices.removeAt(i);
      cortou = true;
      break;
    }
    // Poligono que se cruza sozinho: para em vez de girar para sempre.
    if (!cortou) break;
  }
  if (indices.length == 3) {
    out.add([indices[0], indices[1], indices[2]]);
  }
  return out;
}

/// Amostra o contorno de um [Path] ja construido.
///
/// Pega SO o maior sub-caminho: uma forma com furo (a letra "o") tem
/// dois contornos, e juntar os dois numa lista so faria a triangulacao
/// costurar o furo ao lado de fora.
List<Offset> outlineOfPath(Path path) {
  final metrics = path.computeMetrics().toList();
  if (metrics.isEmpty) return const [];
  var maior = metrics.first;
  for (final m in metrics) {
    if (m.length > maior.length) maior = m;
  }
  final out = <Offset>[];
  final n = math.max(8, (maior.length / 6).round());
  for (var i = 0; i < n; i++) {
    final tan = maior.getTangentForOffset(maior.length * i / n);
    if (tan != null) out.add(tan.position);
  }
  return out;
}

/// Amostra o contorno de um caminho em pontos.
List<Offset> samplePathOutline(BezierPath path, {int perSegment = 10}) {
  final metric = path.build().computeMetrics().toList();
  final out = <Offset>[];
  for (final m in metric) {
    final n = math.max(3, (m.length / 8).round());
    for (var i = 0; i < n; i++) {
      final tan = m.getTangentForOffset(m.length * i / n);
      if (tan != null) out.add(tan.position);
    }
  }
  return out;
}

/// Tira pontos praticamente repetidos — vertice duplicado vira
/// triangulo de area zero, e triangulo de area zero pisca na tela.
List<Offset> dedupeOutline(List<Offset> pts, {double epsilon = 0.5}) {
  final out = <Offset>[];
  for (final p in pts) {
    if (out.isEmpty || (out.last - p).distance > epsilon) out.add(p);
  }
  while (out.length > 1 && (out.first - out.last).distance <= epsilon) {
    out.removeLast();
  }
  return out;
}

/// A FORMA VIRA VOLUME.
///
/// [depth] e a espessura em unidades da forma. A malha sai NORMALIZADA
/// (meia-extensao ~1), que e a convencao do renderizador: quem da o
/// tamanho final e o `size` do no.
Element3DMesh extrudeOutline(List<Offset> contorno, {double depth = 40}) {
  final pts = dedupeOutline(contorno);
  if (pts.length < 3) return Element3DMesh(const [], const []);

  // Normaliza: centro na origem, maior meia-extensao = 1.
  var minX = double.infinity, minY = double.infinity;
  var maxX = -double.infinity, maxY = -double.infinity;
  for (final p in pts) {
    minX = math.min(minX, p.dx);
    minY = math.min(minY, p.dy);
    maxX = math.max(maxX, p.dx);
    maxY = math.max(maxY, p.dy);
  }
  final cx = (minX + maxX) / 2;
  final cy = (minY + maxY) / 2;
  final meia = math.max(
      1e-6, math.max((maxX - minX) / 2, (maxY - minY) / 2));
  final z = (depth / 2) / meia;

  final planos = [
    for (final p in pts) Offset((p.dx - cx) / meia, (p.dy - cy) / meia)
  ];
  final n = planos.length;

  final verts = <List<double>>[
    for (final p in planos) [p.dx, p.dy, z],
    for (final p in planos) [p.dx, p.dy, -z],
  ];

  final faces = <List<int>>[];

  // Tampa da frente e tampa de tras (a de tras com a ordem invertida,
  // para a normal apontar para fora dos dois lados).
  final tampa = earClip(planos);
  for (final t in tampa) {
    faces.add([t[0], t[1], t[2]]);
    faces.add([t[2] + n, t[1] + n, t[0] + n]);
  }

  // Paredes: um quadrilatero por aresta do contorno.
  for (var i = 0; i < n; i++) {
    final j = (i + 1) % n;
    faces.add([i, j, j + n, i + n]);
  }

  return Element3DMesh(verts, faces);
}

/// Atalho: caminho -> volume.
Element3DMesh extrudePath(BezierPath path, {double depth = 40}) =>
    extrudeOutline(samplePathOutline(path), depth: depth);
