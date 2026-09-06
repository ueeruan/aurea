import 'dart:math' as math;
import 'dart:ui';

import 'keyframe.dart';
import 'mask.dart';
import 'shape.dart';
import 'svg_path.dart';

/// A BIBLIOTECA DE FORMAS do menu de adicionar (modelo Alight Motion:
/// grade de 7 por linha, em paginas). Cada entrada e um nome e um
/// construtor dos itens da camada. As formas com numero (retangulo,
/// elipse, poligono, estrela, setor, anel) sao PARAMETRICAS — todo
/// numero delas e animavel; as desenhadas (cruz, crescente, balao,
/// nuvem, linha, X...) nascem como caminho bezier, editavel ponto a
/// ponto no Edit Points.
class ShapeLibraryEntry {
  const ShapeLibraryEntry(this.nome, this.build);

  final String nome;
  final List<ShapeItem> Function() build;
}

/// A ordem observada no Alight, com as que a Aurea ja tinha no fim.
final List<ShapeLibraryEntry> shapeLibrary = [
  ShapeLibraryEntry('Circulo', ShapePresets.paramEllipse),
  ShapeLibraryEntry('Quadrado arredondado', ShapeLibrary.roundedSquare),
  ShapeLibraryEntry('Cruz', ShapeLibrary.cross),
  ShapeLibraryEntry('Crescente', ShapeLibrary.crescent),
  ShapeLibraryEntry('Triangulo', ShapeLibrary.triangle),
  ShapeLibraryEntry('Balao', ShapeLibrary.speechBubble),
  ShapeLibraryEntry('Gota', ShapePresets.drop),
  ShapeLibraryEntry('Pizza', ShapeLibrary.pie),
  ShapeLibraryEntry('Hexagono', ShapeLibrary.hexagon),
  ShapeLibraryEntry('Anel', ShapePresets.paramRing),
  ShapeLibraryEntry('Seta', ShapePresets.arrow),
  ShapeLibraryEntry('Setor', ShapePresets.paramSector),
  ShapeLibraryEntry('Nuvem', ShapeLibrary.cloud),
  ShapeLibraryEntry('Quadrado', ShapeLibrary.square),
  ShapeLibraryEntry('Estrela', ShapePresets.paramStar),
  ShapeLibraryEntry('Linha', ShapeLibrary.line),
  ShapeLibraryEntry('X', ShapeLibrary.xMark),
  ShapeLibraryEntry('Triangulo reto', ShapeLibrary.rightTriangle),
  ShapeLibraryEntry('Pontos', ShapeLibrary.dotPattern),
  ShapeLibraryEntry('Coracao', ShapePresets.heart),
  ShapeLibraryEntry('Engrenagem', ShapePresets.gear),
  ShapeLibraryEntry('Check', ShapePresets.check),
  ShapeLibraryEntry('Flor', ShapePresets.flower),
  ShapeLibraryEntry('Faisca', ShapePresets.sparkle),
  ShapeLibraryEntry('Onda', ShapePresets.wave),
  ShapeLibraryEntry('Arco', ShapePresets.arc),
  ShapeLibraryEntry('Retangulo', ShapePresets.paramRect),
  ShapeLibraryEntry('Poligono', ShapePresets.paramPolygon),
  ShapeLibraryEntry('Cursor seta', ShapeLibrary.cursorArrow),
  ShapeLibraryEntry('Cursor mao', ShapeLibrary.cursorHand),
];

/// Construtores das formas novas.
class ShapeLibrary {
  ShapeLibrary._();

  /// Cursores sao Beziers normais: cor, contorno e pontos ficam editaveis.
  static List<ShapeItem> cursorArrow() => [
    ShapeBezier(
      path: AnimatedPath(
        svgPathToBezier(
          'M -22 -32 L 23 8 L 4 10 L 17 32 L 5 39 L -8 16 L -22 31 Z',
        ),
      ),
    ),
    ShapeFill(color: const Color(0xFF080909)),
    ShapeStroke(
      color: const Color(0xFFFFFFFF),
      width: AnimatedDouble(1.4),
      join: StrokeJoin.round,
    ),
  ];

  static List<ShapeItem> cursorHand() => [
    ShapeBezier(
      path: AnimatedPath(
        svgPathToBezier(
          'M -12 24 L -31 -1 C -40 -13 -31 -22 -23 -16 L -13 -7 '
          'L -17 -43 C -18 -56 -5 -59 -2 -46 L 4 -23 '
          'C 9 -32 19 -29 19 -19 C 25 -27 35 -23 34 -12 '
          'C 42 -21 49 -14 47 -3 L 44 20 Q 43 28 36 33 '
          'L 36 43 L 22 43 L 16 36 L 12 43 L -4 43 L -5 33 Z',
        ),
      ),
    ),
    ShapeFill(color: const Color(0xFFFFFFFF)),
    ShapeStroke(
      color: const Color(0xFF070808),
      width: AnimatedDouble(4),
      join: StrokeJoin.round,
    ),
    for (final x in [5.0, 17.0, 29.0])
      ShapeBezier(
        path: AnimatedPath(
          BezierPath(
            closed: false,
            vertices: [
              PathVertex(p: Offset(x, 1)),
              PathVertex(p: Offset(x + 1, 21)),
            ],
          ),
        ),
      ),
    ShapeStroke(
      color: const Color(0xFF070808),
      width: AnimatedDouble(3),
      cap: StrokeCap.round,
    ),
  ];

  static const Color _corPadrao = Color(0xFF4A7BA6);

  static ShapeBezier _bezier(List<PathVertex> vs, {bool closed = true}) =>
      ShapeBezier(
        path: AnimatedPath(BezierPath(vertices: vs, closed: closed)),
      );

  static PathVertex _v(double x, double y) => PathVertex(p: Offset(x, y));

  /// Quadrado com o raio em 25%: e o "Rounded Rectangle" da AM — animar
  /// o raio de 0 a 100% vira o quadrado em circulo.
  static List<ShapeItem> roundedSquare() => [
    ShapeParametric(
      kind: ParamShapeKind.rect,
      sizeX: AnimatedDouble(320),
      sizeY: AnimatedDouble(320),
      roundness: AnimatedDouble(25),
      roundnessPercent: true,
    ),
    ShapeFill(color: const Color(0xFFFFFFFF)),
  ];

  static List<ShapeItem> square() => [
    ShapeParametric(
      kind: ParamShapeKind.rect,
      sizeX: AnimatedDouble(320),
      sizeY: AnimatedDouble(320),
      roundness: AnimatedDouble(0),
      roundnessPercent: true,
    ),
    ShapeFill(color: _corPadrao),
  ];

  static List<ShapeItem> triangle() => [
    ShapeParametric(
      kind: ParamShapeKind.polygon,
      sizeX: AnimatedDouble(340),
      sizeY: AnimatedDouble(340),
      points: AnimatedDouble(3),
      outerRadius: AnimatedDouble(170),
      roundness: AnimatedDouble(0),
      roundnessPercent: true,
    ),
    ShapeFill(color: const Color(0xFFFFB020)),
  ];

  static List<ShapeItem> hexagon() => [
    ShapeParametric(
      kind: ParamShapeKind.polygon,
      sizeX: AnimatedDouble(340),
      sizeY: AnimatedDouble(340),
      points: AnimatedDouble(6),
      outerRadius: AnimatedDouble(170),
      roundness: AnimatedDouble(0),
      roundnessPercent: true,
    ),
    ShapeFill(color: const Color(0xFF7C62FF)),
  ];

  /// Pizza: um setor de 90 graus sem miolo.
  static List<ShapeItem> pie() => [
    ShapeParametric(
      kind: ParamShapeKind.sector,
      sizeX: AnimatedDouble(340),
      sizeY: AnimatedDouble(340),
      outerRadius: AnimatedDouble(170),
      startAngle: AnimatedDouble(-90),
      sweep: AnimatedDouble(270),
      sectorInner: AnimatedDouble(0),
    ),
    ShapeFill(color: const Color(0xFFFF7A18)),
  ];

  /// Cruz grega: doze cantos, bracos de um terco.
  static List<ShapeItem> cross() {
    const s = 160.0, b = 52.0;
    return [
      _bezier([
        _v(-b, -s), _v(b, -s), _v(b, -b), _v(s, -b), _v(s, b), _v(b, b), //
        _v(b, s), _v(-b, s), _v(-b, b), _v(-s, b), _v(-s, -b), _v(-b, -b),
      ]),
      ShapeFill(color: const Color(0xFFFF3B52)),
    ];
  }

  /// Crescente: dois arcos (o de fora e o de dentro deslocado).
  static List<ShapeItem> crescent() {
    const r = 170.0;
    const k = 0.5523 * r;
    // Arco externo (esquerda, de cima para baixo) + arco interno
    // (volta por dentro, mais a direita).
    return [
      ShapeBezier(
        path: AnimatedPath(
          BezierPath(
            vertices: [
              PathVertex(
                p: const Offset(0, -r),
                inT: const Offset(k * 0.55, 0),
                outT: const Offset(-k, 0),
                corner: false,
              ),
              PathVertex(
                p: const Offset(-r, 0),
                inT: const Offset(0, -k),
                outT: const Offset(0, k),
                corner: false,
              ),
              PathVertex(
                p: const Offset(0, r),
                inT: const Offset(-k, 0),
                outT: const Offset(k * 0.55, 0),
                corner: false,
              ),
              PathVertex(
                p: const Offset(-r * 0.25, 0),
                inT: const Offset(0, k * 0.7),
                outT: const Offset(0, -k * 0.7),
                corner: false,
              ),
            ],
          ),
        ),
      ),
      ShapeFill(color: const Color(0xFFFFD36A)),
    ];
  }

  /// Balao de fala: retangulo arredondado com o rabinho embaixo.
  static List<ShapeItem> speechBubble() {
    const w = 360.0, h = 240.0, r = 60.0;
    const k = 0.5523 * r;
    const w2 = w / 2, h2 = h / 2;
    return [
      ShapeBezier(
        path: AnimatedPath(
          BezierPath(
            vertices: [
              PathVertex(
                p: const Offset(-w2 + r, -h2),
                inT: const Offset(-k, 0),
                corner: false,
              ),
              PathVertex(
                p: const Offset(w2 - r, -h2),
                outT: const Offset(k, 0),
                corner: false,
              ),
              PathVertex(
                p: const Offset(w2, -h2 + r),
                inT: const Offset(0, -k),
                corner: false,
              ),
              PathVertex(
                p: const Offset(w2, h2 - r),
                outT: const Offset(0, k),
                corner: false,
              ),
              PathVertex(
                p: const Offset(w2 - r, h2),
                inT: const Offset(k, 0),
                corner: false,
              ),
              // Rabinho.
              _v(-w2 * 0.15, h2),
              _v(-w2 * 0.42, h2 + 74),
              _v(-w2 * 0.38, h2),
              PathVertex(
                p: const Offset(-w2 + r, h2),
                outT: const Offset(-k, 0),
                corner: false,
              ),
              PathVertex(
                p: const Offset(-w2, h2 - r),
                inT: const Offset(0, k),
                corner: false,
              ),
              PathVertex(
                p: const Offset(-w2, -h2 + r),
                outT: const Offset(0, -k),
                corner: false,
              ),
            ],
          ),
        ),
      ),
      ShapeFill(color: const Color(0xFFFFFFFF)),
    ];
  }

  /// Nuvem: cinco lobos redondos sobre uma base.
  static List<ShapeItem> cloud() {
    final vs = <PathVertex>[];
    void lobo(double cx, double cy, double r, double a0, double a1, int n) {
      for (var i = 0; i <= n; i++) {
        final a = a0 + (a1 - a0) * i / n;
        final p = Offset(cx + r * math.cos(a), cy + r * math.sin(a));
        final t = Offset(-math.sin(a), math.cos(a)) * (r * (a1 - a0) / n / 3);
        vs.add(PathVertex(p: p, inT: -t, outT: t, corner: false));
      }
    }

    lobo(-120, 30, 80, math.pi * 0.55, math.pi * 1.5, 4);
    lobo(-30, -40, 95, math.pi * 1.15, math.pi * 1.9, 4);
    lobo(80, -10, 85, math.pi * 1.25, math.pi * 2.0, 4);
    lobo(140, 50, 60, math.pi * 1.5, math.pi * 2.5, 4);
    vs.add(_v(-40, 110));
    return [_bezier(vs), ShapeFill(color: const Color(0xFFDDE6F2))];
  }

  /// Linha: caminho aberto com traco, sem preenchimento.
  static List<ShapeItem> line() => [
    _bezier([_v(-200, 0), _v(200, 0)], closed: false),
    ShapeStroke(color: const Color(0xFFFFFFFF), width: AnimatedDouble(14)),
  ];

  /// X: duas barras cruzadas, doze cantos.
  static List<ShapeItem> xMark() {
    const s = 160.0, b = 44.0;
    final d = b / math.sqrt(2);
    return [
      _bezier([
        _v(-s + d, -s - d), _v(0, -2 * d), _v(s - d, -s - d), //
        _v(s + d, -s + d), _v(2 * d, 0), _v(s + d, s - d),
        _v(s - d, s + d), _v(0, 2 * d), _v(-s + d, s + d),
        _v(-s - d, s - d), _v(-2 * d, 0), _v(-s - d, -s + d),
      ]),
      ShapeFill(color: const Color(0xFFFF3B52)),
    ];
  }

  static List<ShapeItem> rightTriangle() => [
    _bezier([_v(-160, 160), _v(160, 160), _v(-160, -160)]),
    ShapeFill(color: const Color(0xFF2BE3A0)),
  ];

  /// Padrao de pontos: uma grade 4x4 de bolinhas, cada uma um subcaminho.
  static List<ShapeItem> dotPattern() {
    final items = <ShapeItem>[];
    const passo = 90.0, r = 22.0;
    const k = 0.5523 * r;
    for (var j = 0; j < 4; j++) {
      for (var i = 0; i < 4; i++) {
        final c = Offset(-passo * 1.5 + i * passo, -passo * 1.5 + j * passo);
        items.add(
          ShapeBezier(
            path: AnimatedPath(
              BezierPath(
                vertices: [
                  PathVertex(
                    p: c + const Offset(0, -r),
                    inT: const Offset(-k, 0),
                    outT: const Offset(k, 0),
                    corner: false,
                  ),
                  PathVertex(
                    p: c + const Offset(r, 0),
                    inT: const Offset(0, -k),
                    outT: const Offset(0, k),
                    corner: false,
                  ),
                  PathVertex(
                    p: c + const Offset(0, r),
                    inT: const Offset(k, 0),
                    outT: const Offset(-k, 0),
                    corner: false,
                  ),
                  PathVertex(
                    p: c + const Offset(-r, 0),
                    inT: const Offset(0, k),
                    outT: const Offset(0, -k),
                    corner: false,
                  ),
                ],
              ),
            ),
          ),
        );
      }
    }
    items.add(ShapeFill(color: const Color(0xFFFFFFFF)));
    return items;
  }
}

/// Caminho de amostra de uma entrada da biblioteca, para desenhar o
/// tile. Junta todas as geometrias (o padrao de pontos tem dezesseis).
Path shapeLibraryPreviewPath(List<ShapeItem> items) {
  final out = Path();
  for (final item in items) {
    switch (item) {
      case ShapeBezier b:
        out.addPath(b.buildAt(Duration.zero), Offset.zero);
      case ShapeParametric p:
        out.addPath(p.buildAt(Duration.zero), Offset.zero);
      case ShapePath p:
        out.addPath(p.build(), Offset.zero);
      default:
        break;
    }
  }
  return out;
}

/// A entrada e so traco (linha): o tile desenha com stroke.
bool shapeLibraryIsStrokeOnly(List<ShapeItem> items) =>
    items.any((i) => i is ShapeStroke) &&
    !items.any((i) => i is ShapeFill || i is ShapeGradientFill);

/// SIMPLIFICACAO de um rabisco (Douglas-Peucker) para o desenho livre:
/// tira os pontos que nao mudam a linha mais que [tolerancia] px.
List<Offset> simplifyPolyline(List<Offset> pts, double tolerancia) {
  if (pts.length < 3) return List.of(pts);
  final keep = List<bool>.filled(pts.length, false);
  keep[0] = true;
  keep[pts.length - 1] = true;
  final pilha = <(int, int)>[(0, pts.length - 1)];
  while (pilha.isNotEmpty) {
    final (a, b) = pilha.removeLast();
    var maior = 0.0;
    var idx = -1;
    final pa = pts[a], pb = pts[b];
    final d = pb - pa;
    final len2 = d.dx * d.dx + d.dy * d.dy;
    for (var i = a + 1; i < b; i++) {
      double dist;
      if (len2 < 1e-9) {
        dist = (pts[i] - pa).distance;
      } else {
        final t = (((pts[i] - pa).dx * d.dx + (pts[i] - pa).dy * d.dy) / len2)
            .clamp(0.0, 1.0);
        dist = (pts[i] - (pa + d * t)).distance;
      }
      if (dist > maior) {
        maior = dist;
        idx = i;
      }
    }
    if (idx >= 0 && maior > tolerancia) {
      keep[idx] = true;
      pilha.add((a, idx));
      pilha.add((idx, b));
    }
  }
  return [
    for (var i = 0; i < pts.length; i++)
      if (keep[i]) pts[i],
  ];
}

/// Rabisco -> caminho suave: pontos simplificados viram vertices suaves
/// com tangentes Catmull-Rom (um terco da corda entre os vizinhos).
BezierPath freehandToPath(List<Offset> pts, {double tolerancia = 4}) {
  final s = simplifyPolyline(pts, tolerancia);
  if (s.length < 2) return BezierPath(vertices: const [], closed: false);
  final vs = <PathVertex>[];
  for (var i = 0; i < s.length; i++) {
    final prev = s[i == 0 ? 0 : i - 1];
    final next = s[i == s.length - 1 ? s.length - 1 : i + 1];
    final t = (next - prev) / 6;
    vs.add(
      PathVertex(
        p: s[i],
        inT: i == 0 ? Offset.zero : -t,
        outT: i == s.length - 1 ? Offset.zero : t,
        corner: false,
      ),
    );
  }
  return BezierPath(vertices: vs, closed: false);
}
