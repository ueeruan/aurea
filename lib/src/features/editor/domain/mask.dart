import 'dart:math' as math;
import 'dart:ui';

import 'package:uuid/uuid.dart';

import 'keyframe.dart';

/// Mascaras e caminhos bezier (spec AM2-mascaras-e-formas):
/// - [BezierPath] e O tipo unico de caminho (PR-M1): forma, mascara e — no
///   futuro — caminho de movimento e de texto compartilham esta geometria.
/// - [AnimatedPath] anima o caminho por keyframe com easing por segmento,
///   interpolando vertice a vertice; contagens diferentes sao igualadas
///   por SUBDIVISAO de bezier (de Casteljau em t=0,5) — que preserva a
///   forma exatamente, entao o quadro inicial e o final nao deformam.
/// - [LayerMask] corta o alfa da PROPRIA camada (modelo AE);
/// - [MatteMode] usa OUTRA camada como recorte (modelo Alight).
///   Sistemas separados de proposito — nao unificar.

/// ------------------------------------------------------------- vertices

class PathVertex {
  const PathVertex({
    required this.p,
    this.inT = Offset.zero,
    this.outT = Offset.zero,
    this.corner = true,
  });

  /// Ancora, e tangentes RELATIVAS a ancora.
  final Offset p;
  final Offset inT;
  final Offset outT;
  final bool corner;

  static PathVertex lerp(PathVertex a, PathVertex b, double t) => PathVertex(
    p: Offset.lerp(a.p, b.p, t)!,
    inT: Offset.lerp(a.inT, b.inT, t)!,
    outT: Offset.lerp(a.outT, b.outT, t)!,
    corner: t < 0.5 ? a.corner : b.corner,
  );
}

/// ------------------------------------------------------------ BezierPath

class BezierPath {
  BezierPath({required List<PathVertex> vertices, this.closed = true})
    : vertices = List.unmodifiable(vertices);

  final List<PathVertex> vertices;
  final bool closed;

  bool get isEmpty => vertices.isEmpty;

  Path? _cache;

  /// Cacheado: a instancia e imutavel (zero alocacao por frame).
  Path build() => _cache ??= _buildNow();

  Path _buildNow() {
    final path = Path();
    if (vertices.isEmpty) return path;
    final v0 = vertices.first;
    path.moveTo(v0.p.dx, v0.p.dy);
    for (var i = 1; i < vertices.length; i++) {
      final a = vertices[i - 1];
      final b = vertices[i];
      path.cubicTo(
        a.p.dx + a.outT.dx,
        a.p.dy + a.outT.dy,
        b.p.dx + b.inT.dx,
        b.p.dy + b.inT.dy,
        b.p.dx,
        b.p.dy,
      );
    }
    if (closed && vertices.length > 1) {
      final a = vertices.last;
      final b = vertices.first;
      path.cubicTo(
        a.p.dx + a.outT.dx,
        a.p.dy + a.outT.dy,
        b.p.dx + b.inT.dx,
        b.p.dy + b.inT.dy,
        b.p.dx,
        b.p.dy,
      );
      path.close();
    }
    return path;
  }

  /// Area sinalizada dos pontos ancora: o SINAL da o sentido do caminho.
  double signedArea() {
    var area = 0.0;
    for (var i = 0; i < vertices.length; i++) {
      final a = vertices[i].p;
      final b = vertices[(i + 1) % vertices.length].p;
      area += a.dx * b.dy - b.dx * a.dy;
    }
    return area / 2;
  }

  /// Inverte o sentido (troca tangentes de entrada/saida).
  BezierPath reversed() => BezierPath(
    closed: closed,
    vertices: [
      for (final v in vertices.reversed)
        PathVertex(p: v.p, inT: v.outT, outT: v.inT, corner: v.corner),
    ],
  );

  int get _segmentCount =>
      closed ? vertices.length : math.max(0, vertices.length - 1);

  /// Comprimento aproximado do segmento [i] (corda + poligono)/2.
  double _segLength(int i) {
    final a = vertices[i];
    final b = vertices[(i + 1) % vertices.length];
    final p0 = a.p;
    final p1 = a.p + a.outT;
    final p2 = b.p + b.inT;
    final p3 = b.p;
    final chord = (p3 - p0).distance;
    final poly = (p1 - p0).distance + (p2 - p1).distance + (p3 - p2).distance;
    return (chord + poly) / 2;
  }

  /// Divide o segmento [i] em t=0,5 (de Casteljau) — a FORMA nao muda.
  BezierPath _splitSegment(int i) {
    final a = vertices[i];
    final j = (i + 1) % vertices.length;
    final b = vertices[j];
    final p0 = a.p;
    final p1 = a.p + a.outT;
    final p2 = b.p + b.inT;
    final p3 = b.p;
    final m01 = Offset.lerp(p0, p1, 0.5)!;
    final m12 = Offset.lerp(p1, p2, 0.5)!;
    final m23 = Offset.lerp(p2, p3, 0.5)!;
    final m012 = Offset.lerp(m01, m12, 0.5)!;
    final m123 = Offset.lerp(m12, m23, 0.5)!;
    final mid = Offset.lerp(m012, m123, 0.5)!;

    final newA = PathVertex(
      p: a.p,
      inT: a.inT,
      outT: m01 - p0,
      corner: a.corner,
    );
    final newMid = PathVertex(
      p: mid,
      inT: m012 - mid,
      outT: m123 - mid,
      corner: false,
    );
    final newB = PathVertex(
      p: b.p,
      inT: m23 - p3,
      outT: b.outT,
      corner: b.corner,
    );

    final out = [...vertices];
    out[i] = newA;
    out[j] = newB;
    out.insert(i + 1, newMid);
    return BezierPath(vertices: out, closed: closed);
  }

  /// Aumenta a contagem ate [n] dividindo sempre o segmento mais longo
  /// (distribuicao por comprimento de arco, sem deformar a geometria).
  BezierPath withVertexCount(int n) {
    var path = this;
    while (path.vertices.length < n) {
      var longest = 0;
      var best = -1.0;
      for (var i = 0; i < path._segmentCount; i++) {
        final len = path._segLength(i);
        if (len > best) {
          best = len;
          longest = i;
        }
      }
      if (path._segmentCount == 0) break;
      path = path._splitSegment(longest);
    }
    return path;
  }

  /// Gira a lista de vertices (caminho fechado) para casar com [other]
  /// pelo menor somatorio de distancias — o "matchOffset" automatico que
  /// evita a estrela girar ao virar circulo.
  BezierPath rotatedToMatch(BezierPath other) {
    if (!closed || vertices.length != other.vertices.length) return this;
    final n = vertices.length;
    var bestK = 0;
    var bestCost = double.infinity;
    for (var k = 0; k < n; k++) {
      var cost = 0.0;
      for (var i = 0; i < n; i++) {
        cost += (vertices[(i + k) % n].p - other.vertices[i].p).distanceSquared;
      }
      if (cost < bestCost) {
        bestCost = cost;
        bestK = k;
      }
    }
    if (bestK == 0) return this;
    return BezierPath(
      closed: true,
      vertices: [for (var i = 0; i < n; i++) vertices[(i + bestK) % n]],
    );
  }

  /// Interpolacao com igualacao de contagem, correcao de sentido e
  /// alinhamento de vertice inicial.
  static BezierPath lerp(BezierPath a, BezierPath b, double t) {
    if (t == 0) return a;
    if (t == 1) return b;
    if (b.isEmpty) return a;
    if (a.isEmpty) return b;
    var from = a;
    var to = b;
    if (from.signedArea() * to.signedArea() < 0) {
      to = to.reversed();
    }
    final n = math.max(from.vertices.length, to.vertices.length);
    from = from.withVertexCount(n);
    to = to.withVertexCount(n);
    to = to.rotatedToMatch(from);
    return BezierPath(
      closed: from.closed,
      vertices: [
        for (var i = 0; i < n; i++)
          PathVertex.lerp(from.vertices[i], to.vertices[i], t),
      ],
    );
  }

  // ------------------------------------------------------------- presets

  static const double _kappa = 0.5522847498;

  static BezierPath rect(double w, double h, {Offset center = Offset.zero}) {
    final w2 = w / 2, h2 = h / 2;
    return BezierPath(
      vertices: [
        PathVertex(p: center + Offset(-w2, -h2)),
        PathVertex(p: center + Offset(w2, -h2)),
        PathVertex(p: center + Offset(w2, h2)),
        PathVertex(p: center + Offset(-w2, h2)),
      ],
    );
  }

  /// Retangulo arredondado em NOS EXATOS: cada canto e um quarto de
  /// circulo, que e uma unica cubica com alca de `r * kappa`. Da oito nos
  /// — dois por canto — em vez de dezenas de amostras. Quando o raio
  /// satura um lado (capsula, circulo), os nos coincidentes se fundem e
  /// as alcas continuam colineares: fica liso, sem no duplicado.
  static BezierPath roundedRect(
    double w,
    double h,
    double r, {
    Offset center = Offset.zero,
  }) {
    final w2 = w / 2, h2 = h / 2;
    final rr = r.clamp(0.0, math.min(w2, h2));
    if (rr <= 0) return rect(w, h, center: center);
    final k = rr * _kappa;
    final brutos = [
      // Sentido horario a partir do topo esquerdo, apos o arco.
      PathVertex(
        p: center + Offset(-w2 + rr, -h2),
        inT: Offset(-k, 0),
        corner: true,
      ),
      PathVertex(
        p: center + Offset(w2 - rr, -h2),
        outT: Offset(k, 0),
        corner: true,
      ),
      PathVertex(
        p: center + Offset(w2, -h2 + rr),
        inT: Offset(0, -k),
        corner: true,
      ),
      PathVertex(
        p: center + Offset(w2, h2 - rr),
        outT: Offset(0, k),
        corner: true,
      ),
      PathVertex(
        p: center + Offset(w2 - rr, h2),
        inT: Offset(k, 0),
        corner: true,
      ),
      PathVertex(
        p: center + Offset(-w2 + rr, h2),
        outT: Offset(-k, 0),
        corner: true,
      ),
      PathVertex(
        p: center + Offset(-w2, h2 - rr),
        inT: Offset(0, k),
        corner: true,
      ),
      PathVertex(
        p: center + Offset(-w2, -h2 + rr),
        outT: Offset(0, -k),
        corner: true,
      ),
    ];
    // Funde vizinhos que cairam no mesmo ponto (lado de comprimento zero):
    // o no fundido herda a alca de entrada do primeiro e a de saida do
    // segundo, que sao colineares — por isso vira liso, nao canto.
    final out = <PathVertex>[];
    for (final v in brutos) {
      if (out.isNotEmpty && (out.last.p - v.p).distance < 1e-6) {
        final a = out.removeLast();
        out.add(PathVertex(p: a.p, inT: a.inT, outT: v.outT, corner: false));
      } else {
        out.add(v);
      }
    }
    if (out.length > 1 && (out.first.p - out.last.p).distance < 1e-6) {
      final a = out.removeLast();
      final b = out.removeAt(0);
      out.insert(
        0,
        PathVertex(p: b.p, inT: a.inT, outT: b.outT, corner: false),
      );
    }
    return BezierPath(vertices: out);
  }

  static BezierPath ellipse(double w, double h, {Offset center = Offset.zero}) {
    final rx = w / 2, ry = h / 2;
    final kx = rx * _kappa, ky = ry * _kappa;
    return BezierPath(
      vertices: [
        PathVertex(
          p: center + Offset(0, -ry),
          inT: Offset(-kx, 0),
          outT: Offset(kx, 0),
          corner: false,
        ),
        PathVertex(
          p: center + Offset(rx, 0),
          inT: Offset(0, -ky),
          outT: Offset(0, ky),
          corner: false,
        ),
        PathVertex(
          p: center + Offset(0, ry),
          inT: Offset(kx, 0),
          outT: Offset(-kx, 0),
          corner: false,
        ),
        PathVertex(
          p: center + Offset(-rx, 0),
          inT: Offset(0, ky),
          outT: Offset(0, -ky),
          corner: false,
        ),
      ],
    );
  }

  static BezierPath star(
    int points,
    double outer,
    double inner, {
    Offset center = Offset.zero,
  }) {
    final n = points * 2;
    return BezierPath(
      vertices: [
        for (var i = 0; i < n; i++)
          PathVertex(
            p:
                center +
                Offset(
                  math.cos(-math.pi / 2 + i * math.pi * 2 / n) *
                      (i.isEven ? outer : inner),
                  math.sin(-math.pi / 2 + i * math.pi * 2 / n) *
                      (i.isEven ? outer : inner),
                ),
          ),
      ],
    );
  }

  static BezierPath heart(double w, double h, {Offset center = Offset.zero}) {
    // Mesmos cubics do ShapePath.heart, expressos como 2 vertices.
    final w2 = w / 2, h2 = h / 2;
    return BezierPath(
      vertices: [
        PathVertex(
          p: center + Offset(0, -h2 * 0.25),
          inT: Offset(w2 * 0.55, -h2 * 0.8),
          outT: Offset(-w2 * 0.55, -h2 * 0.8),
          corner: false,
        ),
        PathVertex(
          p: center + Offset(0, h2 * 0.95),
          inT: Offset(-w2 * 1.05, -h2 * 1.1),
          outT: Offset(w2 * 1.05, -h2 * 1.1),
          corner: true,
        ),
      ],
    );
  }
}

/// ---------------------------------------------------------- AnimatedPath

const _epsilon = Duration(milliseconds: 8);

/// Caminho animavel por keyframe (mesmo modelo dos AnimatedDouble:
/// easing por SEGMENTO guardado no keyframe de saida).
class AnimatedPath {
  AnimatedPath(this.base, [List<Keyframe<BezierPath>>? keyframes])
    : keyframes = List.unmodifiable(
        keyframes ?? const <Keyframe<BezierPath>>[],
      );

  final BezierPath base;
  final List<Keyframe<BezierPath>> keyframes;

  bool get isAnimated => keyframes.isNotEmpty;

  bool hasKeyframeAt(Duration t) =>
      keyframes.any((k) => (k.time - t).abs() < _epsilon);

  BezierPath valueAt(Duration t) {
    if (keyframes.isEmpty) return base;
    if (t <= keyframes.first.time) return keyframes.first.value;
    if (t >= keyframes.last.time) return keyframes.last.value;
    var lo = 0;
    var hi = keyframes.length - 1;
    while (hi - lo > 1) {
      final mid = (lo + hi) >> 1;
      if (keyframes[mid].time <= t) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
    final a = keyframes[lo];
    final b = keyframes[hi];
    final span = (b.time - a.time).inMicroseconds;
    var f = span == 0 ? 1.0 : (t - a.time).inMicroseconds / span;
    f = a.ease.transform(f.clamp(0.0, 1.0));
    return BezierPath.lerp(a.value, b.value, f);
  }

  AnimatedPath withKeyframe(
    Duration t,
    BezierPath v, [
    Easing ease = Easing.linear,
  ]) {
    final out = [
      for (final k in keyframes)
        if ((k.time - t).abs() >= _epsilon) k,
      Keyframe(time: t, value: v, ease: ease),
    ]..sort((a, b) => a.time.compareTo(b.time));
    return AnimatedPath(base, out);
  }

  AnimatedPath withoutKeyframe(Duration t) {
    final rest = [
      for (final k in keyframes)
        if ((k.time - t).abs() >= _epsilon) k,
    ];
    if (rest.isEmpty) return AnimatedPath(valueAt(t));
    return AnimatedPath(base, rest);
  }

  Easing easeAt(Duration t) {
    for (final k in keyframes) {
      if ((k.time - t).abs() < _epsilon) return k.ease;
    }
    return Easing.linear;
  }

  AnimatedPath edited(Duration t, BezierPath v) =>
      isAnimated ? withKeyframe(t, v, easeAt(t)) : AnimatedPath(v, keyframes);
}

/// -------------------------------------------------------------- mascara

/// Modos de mascara (PR-M2). O modo NAO e animavel (trocar no meio nao
/// interpola); para transicao de modo, use duas mascaras com opacidade.
enum MaskMode { none, add, subtract, intersect, lighten, darken, difference }

/// Animacoes prontas de revelacao. Elas continuam sendo mascaras comuns:
/// o preset so escolhe as duas geometrias e grava os keyframes iniciais.
enum MaskRevealPreset { esquerda, direita, cima, baixo, iris, caixa, cortina }

extension MaskRevealPresetLabel on MaskRevealPreset {
  String get label => switch (this) {
    MaskRevealPreset.esquerda => 'Esquerda',
    MaskRevealPreset.direita => 'Direita',
    MaskRevealPreset.cima => 'Cima',
    MaskRevealPreset.baixo => 'Baixo',
    MaskRevealPreset.iris => 'Iris',
    MaskRevealPreset.caixa => 'Caixa',
    MaskRevealPreset.cortina => 'Cortina',
  };
}

class LayerMask {
  LayerMask({
    String? id,
    this.name = 'Mascara',
    this.mode = MaskMode.add,
    this.inverted = false,
    AnimatedPath? path,
    AnimatedDouble? feather,
    this.featherY,
    AnimatedDouble? opacity,
    AnimatedDouble? expansion,
  }) : id = id ?? const Uuid().v4(),
       path = path ?? AnimatedPath(BezierPath.rect(400, 400)),
       feather = feather ?? AnimatedDouble(0),
       opacity = opacity ?? AnimatedDouble(1),
       expansion = expansion ?? AnimatedDouble(0);

  final String id;
  final String name;
  final MaskMode mode;
  final bool inverted;

  /// Caminho ANIMAVEL (PR-M1 aplicado a mascara).
  final AnimatedPath path;

  /// Feather com queda gaussiana, montado em cima da borda. Quando so
  /// este existe, a suavidade e igual nos dois eixos.
  final AnimatedDouble feather;

  /// Suavidade SO NA VERTICAL, quando os eixos estao soltos.
  ///
  /// E o que faz um degrade de horizonte: borda dura dos lados, macia em
  /// cima e embaixo. Nulo = ligada ao eixo X (o caso comum, e o que
  /// mantem o projeto antigo igual).
  final AnimatedDouble? featherY;

  /// Suavidade vertical efetiva — cai no eixo X quando estao ligados.
  AnimatedDouble get featherVertical => featherY ?? feather;

  bool get featherLinked => featherY == null;

  final AnimatedDouble opacity;

  /// Expande/contrai o alcance sem mexer nos vertices.
  final AnimatedDouble expansion;

  bool get hasAnimation =>
      path.isAnimated ||
      feather.isAnimated ||
      (featherY?.isAnimated ?? false) ||
      opacity.isAnimated ||
      expansion.isAnimated;

  LayerMask copyWith({
    String? name,
    MaskMode? mode,
    bool? inverted,
    AnimatedPath? path,
    AnimatedDouble? feather,
    AnimatedDouble? featherY,
    bool linkFeather = false,
    AnimatedDouble? opacity,
    AnimatedDouble? expansion,
  }) {
    return LayerMask(
      id: id,
      name: name ?? this.name,
      mode: mode ?? this.mode,
      inverted: inverted ?? this.inverted,
      path: path ?? this.path,
      feather: feather ?? this.feather,
      featherY: linkFeather ? null : (featherY ?? this.featherY),
      opacity: opacity ?? this.opacity,
      expansion: expansion ?? this.expansion,
    );
  }
}

/// Cria uma revelacao inteiramente editavel: dois keyframes de caminho,
/// sendo o primeiro a saida com mola/overshoot e o segundo a cobertura final.
LayerMask createRevealMask(
  MaskRevealPreset preset,
  Size size,
  Duration localStart, {
  Duration duration = const Duration(milliseconds: 650),
}) {
  final width = size.width.abs() < 1 ? 1.0 : size.width.abs();
  final height = size.height.abs() < 1 ? 1.0 : size.height.abs();
  final fullBox = BezierPath.rect(width, height);

  late final BezierPath initial;
  late final BezierPath finalPath;
  switch (preset) {
    case MaskRevealPreset.esquerda:
      initial = BezierPath.rect(width, height, center: Offset(-width, 0));
      finalPath = fullBox;
    case MaskRevealPreset.direita:
      initial = BezierPath.rect(width, height, center: Offset(width, 0));
      finalPath = fullBox;
    case MaskRevealPreset.cima:
      initial = BezierPath.rect(width, height, center: Offset(0, -height));
      finalPath = fullBox;
    case MaskRevealPreset.baixo:
      initial = BezierPath.rect(width, height, center: Offset(0, height));
      finalPath = fullBox;
    case MaskRevealPreset.iris:
      initial = BezierPath.ellipse(
        math.max(1, width * 0.02),
        math.max(1, height * 0.02),
      );
      // Uma elipse do mesmo tamanho toca os meios das bordas, mas nao os
      // cantos. sqrt(2) garante que a caixa inteira esteja dentro no fim.
      finalPath = BezierPath.ellipse(width * math.sqrt2, height * math.sqrt2);
    case MaskRevealPreset.caixa:
      initial = BezierPath.rect(
        math.max(1, width * 0.02),
        math.max(1, height * 0.02),
      );
      finalPath = fullBox;
    case MaskRevealPreset.cortina:
      initial = BezierPath.rect(math.max(1, width * 0.02), height);
      finalPath = fullBox;
  }

  final path = AnimatedPath(finalPath)
      .withKeyframe(localStart, initial, Easing.elastic)
      .withKeyframe(localStart + duration, finalPath);
  return LayerMask(
    name: 'Revelar ${preset.label}',
    path: path,
    feather: AnimatedDouble(24),
  );
}

/// Se o alcance visual da mascara passa da caixa local da camada.
/// Feather e largura total: metade cai para cada lado da borda.
bool maskFeatherExceedsBounds(LayerMask mask, Duration localTime, Size size) {
  final path = mask.path.valueAt(localTime);
  if (path.vertices.isEmpty) return false;

  final bounds = path.build().getBounds();
  final expansion = mask.expansion.valueAt(localTime);
  final reachX = math.max(0, mask.feather.valueAt(localTime)) / 2 + expansion;
  final reachY =
      math.max(0, mask.featherVertical.valueAt(localTime)) / 2 + expansion;
  final halfWidth = size.width.abs() / 2;
  final halfHeight = size.height.abs() / 2;

  return bounds.left - reachX < -halfWidth ||
      bounds.right + reachX > halfWidth ||
      bounds.top - reachY < -halfHeight ||
      bounds.bottom + reachY > halfHeight;
}

/// Matte por camada (PR-M5, modelo Alight): OUTRA camada recorta esta.
enum MatteMode { none, alpha, alphaInvert, luma, lumaInvert }
