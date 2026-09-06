import 'dart:math' as math;
import 'dart:ui';

/// OPERADORES DE CAMINHO — as transformacoes que faltavam para "forma
/// vetorial" ser verdade inteira.
///
/// Todos trabalham sobre a POLILINHA amostrada do caminho, nao sobre os
/// nos bezier originais. E uma escolha: amostrar perde a bezier exata,
/// mas funciona igual para caminho vindo de SVG, de morph ou de forma
/// parametrica — e nenhum deles expoe os nos do mesmo jeito.
///
/// A densidade de amostragem e o que decide se o resultado fica liso ou
/// facetado, entao ela e parametro, nao numero magico enterrado.

/// Os operadores de caminho oferecidos no menu.
enum ShapePathOp {
  offset,
  roundCorners,
  zigZag,
  puckerBloat,
  twist,
  wiggle,
  merge,
}

String shapePathOpLabel(ShapePathOp o) => switch (o) {
      ShapePathOp.offset => 'Deslocar',
      ShapePathOp.roundCorners => 'Arredondar',
      ShapePathOp.zigZag => 'Zig zag',
      ShapePathOp.puckerBloat => 'Inchar',
      ShapePathOp.twist => 'Torcer',
      ShapePathOp.wiggle => 'Baguncar',
      ShapePathOp.merge => 'Combinar',
    };

String mergeModeLabel(MergeMode m) => switch (m) {
      MergeMode.union => 'Unir',
      MergeMode.subtract => 'Subtrair',
      MergeMode.intersect => 'Interseccao',
      MergeMode.exclude => 'Excluir',
    };

/// Passo de amostragem, em pixels. Menor = mais liso e mais caro.
const shapeSampleStep = 3.0;

/// Amostra um caminho em polilinhas fechadas ou abertas.
List<(List<Offset>, bool)> samplePath(Path path,
    {double step = shapeSampleStep}) {
  final out = <(List<Offset>, bool)>[];
  for (final metric in path.computeMetrics()) {
    final len = metric.length;
    if (len <= 0) continue;
    final n = math.max(2, (len / step).ceil());
    final pts = <Offset>[];
    for (var i = 0; i <= n; i++) {
      final d = (i / n) * len;
      final tan = metric.getTangentForOffset(d);
      if (tan != null) pts.add(tan.position);
    }
    if (pts.length >= 2) out.add((pts, metric.isClosed));
  }
  return out;
}

Path _fromPolys(List<(List<Offset>, bool)> polys) {
  final p = Path();
  for (final (pts, closed) in polys) {
    if (pts.length < 2) continue;
    p.moveTo(pts.first.dx, pts.first.dy);
    for (final pt in pts.skip(1)) {
      p.lineTo(pt.dx, pt.dy);
    }
    if (closed) p.close();
  }
  return p;
}

/// Normal PARA FORA em cada ponto da polilinha.
///
/// Calculada a partir da tangente MEDIA dos dois segmentos vizinhos —
/// usar so um lado faz o resultado tremer nos cantos.
///
/// O sentido da perpendicular depende de o caminho ter sido escrito no
/// sentido horario ou anti-horario, e isso muda entre um `addOval` e um
/// caminho importado de SVG. Entao a normal e virada para longe do
/// centroide: assim "para fora" quer dizer para fora em qualquer
/// caminho, e nao o oposto na metade deles.
List<Offset> _normals(List<Offset> pts, bool closed) {
  final n = pts.length;
  final out = List<Offset>.filled(n, Offset.zero);
  final c = polyCenter(pts);
  for (var i = 0; i < n; i++) {
    final ant = i == 0 ? (closed ? pts[n - 2] : pts[0]) : pts[i - 1];
    final pro = i == n - 1 ? (closed ? pts[1] : pts[n - 1]) : pts[i + 1];
    var t = pro - ant;
    final len = t.distance;
    if (len < 1e-9) {
      out[i] = Offset.zero;
      continue;
    }
    t = Offset(t.dx / len, t.dy / len);
    var nrm = Offset(-t.dy, t.dx);
    final radial = pts[i] - c;
    if (nrm.dx * radial.dx + nrm.dy * radial.dy < 0) {
      nrm = Offset(-nrm.dx, -nrm.dy);
    }
    out[i] = nrm;
  }
  return out;
}

/// Centro medio dos pontos de um caminho.
Offset polyCenter(List<Offset> pts) {
  if (pts.isEmpty) return Offset.zero;
  var x = 0.0, y = 0.0;
  for (final p in pts) {
    x += p.dx;
    y += p.dy;
  }
  return Offset(x / pts.length, y / pts.length);
}

// ------------------------------------------------------ Deslocar

/// DESLOCAR CAMINHO: engorda ou afina a forma, mantendo o contorno.
///
/// Nao e o mesmo que escalar: escalar afasta do centro, deslocar anda na
/// NORMAL de cada ponto. Num retangulo, escalar estica os lados; aqui os
/// quatro lados afastam a mesma distancia, e o canto vira quina ou
/// arredonda conforme a juncao.
Path offsetPath(Path source, double amount, {double step = shapeSampleStep}) {
  if (amount.abs() < 0.01) return source;
  final polys = samplePath(source, step: step);
  final out = <(List<Offset>, bool)>[];
  for (final (pts, closed) in polys) {
    final nrm = _normals(pts, closed);
    out.add((
      [
        for (var i = 0; i < pts.length; i++)
          pts[i] + Offset(nrm[i].dx * amount, nrm[i].dy * amount),
      ],
      closed
    ));
  }
  return _fromPolys(out);
}

// ------------------------------------------- Arredondar cantos

/// ARREDONDAR CANTOS: troca cada quina por um arco.
///
/// O canto e achado pelo ANGULO entre os segmentos vizinhos; onde a
/// direcao muda pouco nao ha o que arredondar, e mexer ali so estragaria
/// a curva que ja era lisa.
///
/// O raio e medido em COMPRIMENTO DE ARCO ao longo da polilinha, nao
/// ate a amostra vizinha. Essa distincao e a diferenca entre funcionar e
/// nao fazer nada: com amostragem de 3 px, a amostra vizinha esta a 3 px
/// e o raio ficaria preso nesse valor.
Path roundCorners(Path source, double radius,
    {double step = shapeSampleStep, double minAngleDeg = 25}) {
  if (radius <= 0.01) return source;
  final polys = samplePath(source, step: step);
  final cosLimite = math.cos((180 - minAngleDeg) * math.pi / 180);
  final out = <(List<Offset>, bool)>[];

  for (final (pts, closed) in polys) {
    final n = pts.length;
    if (n < 5) {
      out.add((pts, closed));
      continue;
    }

    // Quantas amostras cabem no raio pedido.
    var passo = 0.0;
    for (var i = 1; i < n; i++) {
      passo += (pts[i] - pts[i - 1]).distance;
    }
    passo /= (n - 1);
    final span = math.max(1, (radius / math.max(0.01, passo)).round());
    if (span * 2 + 1 >= n) {
      out.add((pts, closed));
      continue;
    }

    // Marca os cantos: giro grande entre o que vem e o que vai.
    final canto = List<bool>.filled(n, false);
    for (var i = 0; i < n; i++) {
      final ai = i - 1 < 0 ? (closed ? n - 2 : 0) : i - 1;
      final bi = i + 1 >= n ? (closed ? 1 : n - 1) : i + 1;
      var a = pts[ai] - pts[i];
      var b = pts[bi] - pts[i];
      final la = a.distance, lb = b.distance;
      if (la < 1e-6 || lb < 1e-6) continue;
      a = Offset(a.dx / la, a.dy / la);
      b = Offset(b.dx / lb, b.dy / lb);
      final cos = a.dx * b.dx + a.dy * b.dy;
      // cos perto de -1 = quase reto. Canto e quando sobe disso.
      if (cos > cosLimite) canto[i] = true;
    }

    // Decide ANTES de emitir qual faixa cada canto engole. Emitir e
    // apagar ao mesmo tempo nao funciona: depois do primeiro canto o
    // tamanho da lista deixa de bater com o indice original, e o corte
    // cai no lugar errado.
    final arco = <int, List<Offset>>{};
    final engolido = List<bool>.filled(n, false);
    for (var i = 0; i < n; i++) {
      if (!canto[i]) continue;
      final ai = i - span;
      final bi = i + span;
      if (ai < 0 || bi >= n) continue;
      // Dois cantos perto demais: o primeiro fica, o segundo espera.
      var livre = true;
      for (var k = ai; k <= bi; k++) {
        if (engolido[k]) livre = false;
      }
      if (!livre) continue;

      final a = pts[ai], b = pts[bi], ctrl = pts[i];
      const passos = 8;
      arco[ai] = [
        for (var k = 0; k <= passos; k++)
          () {
            final u = k / passos;
            final iu = 1 - u;
            return Offset(
              iu * iu * a.dx + 2 * iu * u * ctrl.dx + u * u * b.dx,
              iu * iu * a.dy + 2 * iu * u * ctrl.dy + u * u * b.dy,
            );
          }(),
      ];
      for (var k = ai; k <= bi; k++) {
        engolido[k] = true;
      }
    }

    final novo = <Offset>[];
    for (var i = 0; i < n; i++) {
      final a = arco[i];
      if (a != null) {
        novo.addAll(a);
        continue;
      }
      if (!engolido[i]) novo.add(pts[i]);
    }
    out.add((novo, closed));
  }
  return _fromPolys(out);
}

// ------------------------------------------------------- Zig Zag

/// ZIG ZAG: alterna os pontos para dentro e para fora da normal.
///
/// [ridges] e quantas cristas por segmento amostrado — e o que decide se
/// sai uma serra fina ou uma onda larga.
Path zigZag(Path source, double amplitude, double ridges,
    {double step = shapeSampleStep, bool smooth = false}) {
  if (amplitude.abs() < 0.01) return source;
  final polys = samplePath(source, step: step);
  final out = <(List<Offset>, bool)>[];
  final freq = math.max(0.1, ridges);

  for (final (pts, closed) in polys) {
    final nrm = _normals(pts, closed);
    final novo = <Offset>[];
    for (var i = 0; i < pts.length; i++) {
      final fase = i * freq;
      // Serra: alterna seco. Onda: seno.
      final k = smooth
          ? math.sin(fase * math.pi)
          : (fase.floor().isEven ? 1.0 : -1.0);
      novo.add(pts[i] +
          Offset(nrm[i].dx * amplitude * k, nrm[i].dy * amplitude * k));
    }
    out.add((novo, closed));
  }
  return _fromPolys(out);
}

// --------------------------------------------- Inchar e encolher

/// INCHAR E ENCOLHER: puxa os pontos para o centro ou para fora dele,
/// com forca proporcional a distancia.
///
/// Positivo incha (a forma fica bojuda), negativo encolhe (fica de
/// estrela). E o operador que transforma circulo em flor.
Path puckerBloat(Path source, double amount,
    {double step = shapeSampleStep}) {
  if (amount.abs() < 0.001) return source;
  final polys = samplePath(source, step: step);
  final out = <(List<Offset>, bool)>[];

  for (final (pts, closed) in polys) {
    final c = polyCenter(pts);
    // Raio medio: e a referencia que decide o que "para fora" significa.
    var raio = 0.0;
    for (final p in pts) {
      raio += (p - c).distance;
    }
    raio /= pts.length;
    if (raio < 1e-6) {
      out.add((pts, closed));
      continue;
    }

    final novo = <Offset>[];
    for (final p in pts) {
      final v = p - c;
      final d = v.distance;
      if (d < 1e-9) {
        novo.add(p);
        continue;
      }
      // Duas parcelas somadas, e as duas precisam existir:
      //
      //   uniforme  — um circulo, onde todo ponto esta a mesma
      //               distancia, tem de inchar mesmo assim; so a
      //               parcela de desvio o deixaria parado
      //   desvio    — o que ja esta longe do centro vai mais longe, e o
      //               que esta perto encolhe. E o que transforma
      //               estrela em flor
      final novoD = d + amount * (raio * 0.5 + (d - raio));
      final f = novoD / d;
      novo.add(c + Offset(v.dx * f, v.dy * f));
    }
    out.add((novo, closed));
  }
  return _fromPolys(out);
}

// -------------------------------------------------------- Torcer

/// TORCER: gira cada ponto em torno do centro, com angulo proporcional a
/// distancia. O centro fica parado e a borda gira inteiro.
Path twist(Path source, double angleDeg, {double step = shapeSampleStep}) {
  if (angleDeg.abs() < 0.01) return source;
  final polys = samplePath(source, step: step);
  final todos = [for (final (pts, _) in polys) ...pts];
  if (todos.isEmpty) return source;
  final c = polyCenter(todos);
  var raio = 0.0;
  for (final p in todos) {
    final d = (p - c).distance;
    if (d > raio) raio = d;
  }
  if (raio < 1e-6) return source;

  final rad = angleDeg * math.pi / 180;
  final out = <(List<Offset>, bool)>[];
  for (final (pts, closed) in polys) {
    out.add((
      [
        for (final p in pts)
          () {
            final v = p - c;
            final d = v.distance;
            final a = rad * (d / raio);
            final cos = math.cos(a), sin = math.sin(a);
            return c +
                Offset(v.dx * cos - v.dy * sin, v.dx * sin + v.dy * cos);
          }(),
      ],
      closed
    ));
  }
  return _fromPolys(out);
}

// ------------------------------------------- Baguncar o caminho

/// BAGUNCAR O CAMINHO: desloca cada ponto por ruido.
///
/// O ruido e funcao pura de (semente, indice), entao o mesmo instante da
/// sempre o mesmo caminho — exportar duas vezes nao pode dar formas
/// diferentes. [evolution] anima a bagunca sem sortear de novo.
Path wigglePath(
  Path source,
  double amount, {
  int seed = 1,
  double detail = 1,
  double evolution = 0,
  double step = shapeSampleStep,
}) {
  if (amount.abs() < 0.01) return source;
  final polys = samplePath(source, step: step);
  final out = <(List<Offset>, bool)>[];

  for (final (pts, closed) in polys) {
    final nrm = _normals(pts, closed);
    final novo = <Offset>[];
    for (var i = 0; i < pts.length; i++) {
      final x = i * math.max(0.05, detail) + evolution;
      final n = _valueNoise(x, seed) * 2 - 1;
      novo.add(pts[i] +
          Offset(nrm[i].dx * amount * n, nrm[i].dy * amount * n));
    }
    out.add((novo, closed));
  }
  return _fromPolys(out);
}

double _hash01(int x) {
  var h = x * 374761393 + 668265263;
  h = (h ^ (h >> 13)) * 1274126177;
  return ((h ^ (h >> 16)) & 0x7fffffff) / 0x7fffffff;
}

double _valueNoise(double x, int seed) {
  final i = x.floor();
  final f = x - i;
  final a = _hash01(i + seed * 7919);
  final b = _hash01(i + 1 + seed * 7919);
  final s = f * f * (3 - 2 * f);
  return a + (b - a) * s;
}

// -------------------------------------------------- Combinar

/// Como dois caminhos se combinam.
enum MergeMode { union, subtract, intersect, exclude }

/// COMBINAR CAMINHOS: as operacoes booleanas.
///
/// E o que permite fazer um furo de verdade numa forma, em vez de pintar
/// por cima com a cor do fundo — que quebra assim que a camada de baixo
/// muda.
Path mergePaths(List<Path> paths, MergeMode mode) {
  if (paths.isEmpty) return Path();
  if (paths.length == 1) return paths.first;

  final op = switch (mode) {
    MergeMode.union => PathOperation.union,
    MergeMode.subtract => PathOperation.difference,
    MergeMode.intersect => PathOperation.intersect,
    MergeMode.exclude => PathOperation.xor,
  };

  var acc = paths.first;
  for (final p in paths.skip(1)) {
    acc = Path.combine(op, acc, p);
  }
  return acc;
}
