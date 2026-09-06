import 'dart:math' as math;
import 'dart:ui';

import '../../editor/domain/effect.dart';
import '../../editor/domain/element3d.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/layer_meta.dart';
import '../../editor/domain/mask.dart';
import '../../editor/domain/shape.dart';
import '../../editor/domain/svg_path.dart';
import '../../editor/domain/video_project.dart';
import 'reference_motion_samples.dart';

/// Nova composicao, desenhada a partir do MP4 fornecido. Nao depende do
/// modelo Pindown anterior. Formas, curvas, luzes e tempos ficam editaveis.
/// Os detalhes modelados sao uma reconstrucao, nao pixels do video-fonte.
Duration referenceFrame(int n) =>
    Duration(microseconds: (n * 1e6 / 30).round());

double _track(List<(int, double)> points, int frame) {
  if (frame <= points.first.$1) return points.first.$2;
  for (var i = 1; i < points.length; i++) {
    final a = points[i - 1], b = points[i];
    if (frame <= b.$1) {
      final t = (frame - a.$1) / (b.$1 - a.$1);
      return a.$2 + (b.$2 - a.$2) * t;
    }
  }
  return points.last.$2;
}

AnimatedDouble _values(int start, int end, double Function(int) value) =>
    AnimatedDouble(value(start), [
      for (var q = start; q < end; q++)
        Keyframe(
          time: referenceFrame(q) - referenceFrame(start),
          value: value(q),
        ),
    ]);

BezierPath _map(
  BezierPath p,
  Offset Function(Offset) point, {
  double sx = 1,
  double sy = 1,
}) => BezierPath(
  closed: p.closed,
  vertices: [
    for (final v in p.vertices)
      PathVertex(
        p: point(v.p),
        inT: Offset(v.inT.dx * sx, v.inT.dy * sy),
        outT: Offset(v.outT.dx * sx, v.outT.dy * sy),
        corner: v.corner,
      ),
  ],
);

BezierPath _svg(String data) => svgPathToBezier(data);
BezierPath _rect(double x, double y, double w, double h) =>
    _map(BezierPath.rect(w, h), (p) => p + Offset(x + w / 2, y + h / 2));
BezierPath _ellipse(double x, double y, double w, double h) =>
    _map(BezierPath.ellipse(w, h), (p) => p + Offset(x, y));

ShapeGradientFill _gradient(
  List<int> colors, {
  double angle = 90,
  List<double>? stops,
  bool radial = false,
  Offset center = Offset.zero,
  double radius = 1,
}) => ShapeGradientFill(
  colorA: Color(colors.first),
  colorB: Color(colors.last),
  extras: [for (final c in colors.skip(1).take(colors.length - 2)) Color(c)],
  stops: stops,
  angleDeg: angle,
  radial: radial,
  center: center,
  radiusScale: radius,
);

EffectInstance _glow(Color color, double radius, double gain) => EffectInstance(
  type: EffectType.lightGlow,
  color: color,
  params: {
    'raio': AnimatedDouble(radius),
    'intensity': AnimatedDouble(gain),
    'threshold': AnimatedDouble(40),
    'piramide': AnimatedDouble(3),
    'mult_r': AnimatedDouble(1),
    'mult_g': AnimatedDouble(1),
    'mult_b': AnimatedDouble(1),
    'mesclagem': AnimatedDouble(1),
  },
);

/// Bake geometry, not pixels: every source frame is a normal editable
/// Bezier keyframe. Centers follow bounds because ShapeView centers its paint.
ShapeLayer _draw(
  String id,
  String name,
  int start,
  int end,
  BezierPath Function(int) shape,
  ShapeItem fill, {
  List<EffectInstance>? fx,
  double Function(int)? opacity,
}) {
  final shapes = [for (var q = start; q < end; q++) shape(q)];
  final centers = [for (final p in shapes) p.build().getBounds().center];
  final local = [
    for (var i = 0; i < shapes.length; i++)
      _map(shapes[i], (p) => p - centers[i]),
  ];
  return ShapeLayer(
    id: 'rebuild_$id',
    name: name,
    startTime: referenceFrame(start),
    duration: referenceFrame(end) - referenceFrame(start),
    position: AnimatedOffset(centers.first, [
      for (var i = 0; i < centers.length; i++)
        Keyframe(
          time: referenceFrame(start + i) - referenceFrame(start),
          value: centers[i],
        ),
    ]),
    contents: [
      ShapeBezier(
        id: 'rebuild_${id}_path',
        path: AnimatedPath(local.first, [
          for (var i = 0; i < local.length; i++)
            Keyframe(
              time: referenceFrame(start + i) - referenceFrame(start),
              value: local[i],
            ),
        ]),
      ),
      fill,
    ],
    effects: fx,
    opacity: opacity == null ? null : _values(start, end, opacity),
  );
}

ShapeLayer _static(
  String id,
  String name,
  int start,
  int end,
  BezierPath shape,
  ShapeItem fill, {
  List<EffectInstance>? fx,
}) {
  final center = shape.build().getBounds().center;
  return ShapeLayer(
    id: 'rebuild_$id',
    name: name,
    startTime: referenceFrame(start),
    duration: referenceFrame(end) - referenceFrame(start),
    position: AnimatedOffset(center),
    effects: fx,
    contents: [
      ShapeBezier(
        id: 'rebuild_${id}_path',
        path: AnimatedPath(_map(shape, (p) => p - center)),
      ),
      fill,
    ],
  );
}

double _houseX(int q) {
  if (q < 4) return _track([(0, 335), (4, 220)], q);
  final row = referenceHouseSamples.where((r) => r[0] == q).firstOrNull;
  if (row != null) return (row[1] + row[3]) / 2;
  return _track([(43, 406), (54, 392), (61, 386), (69, 382)], q);
}

double _fire(int q) =>
    q >= 62 ||
        q == 45 ||
        q == 46 ||
        q == 50 ||
        q == 51 ||
        q == 52 ||
        q == 56 ||
        q == 57 ||
        q == 58
    ? 1
    : 0;

List<Layer> _houseScene() {
  final out = <Layer>[];
  out.add(
    _static(
      'sky',
      '01 · Ceu noturno',
      0,
      70,
      _rect(0, 0, 720, 1278),
      _gradient([0xFF020A16, 0xFF04152C, 0xFF133355]),
    ),
  );
  out.add(
    _draw(
      'sky_light',
      '01 · Luz atras da casa',
      0,
      70,
      (q) => _ellipse(_houseX(q) - 15, 795, 1100, 1250),
      _gradient([0xC83C82DA, 0x00204374], radial: true, radius: .78),
      opacity: (q) => 1 - _fire(q) * .5,
    ),
  );
  out.add(
    _draw(
      'floor',
      '01 · Horizonte em movimento',
      0,
      70,
      (q) => _rect(
        0,
        807,
        720,
        _track([(0, 340), (12, 480), (43, 580), (69, 340)], q),
      ),
      _gradient(
        [
          0xFF0B212B,
          0xFF23486E,
          0xFF31C895,
          0xFF47FFC4,
          0xFF76FFFD,
          0xFFF4FDFD,
        ],
        stops: [0, .27, .51, .65, .81, 1],
      ),
    ),
  );
  out.add(
    _static(
      'floor_base',
      '01 · Base do horizonte',
      0,
      70,
      _rect(0, 807, 720, 471),
      _gradient(
        [0xFF0B212B, 0xFF255881, 0xFF3EF5A5, 0xFF8FFFF9, 0xFFF4FDFD],
        stops: [0, .35, .63, .82, 1],
      ),
    ),
  );
  // Put the moving horizon over the fixed lower continuation.
  final floor = out.removeAt(2);
  out.add(floor);
  out.add(
    _draw(
      'fire_light',
      '02 · Iluminacao vermelha',
      44,
      70,
      (_) => _rect(0, 0, 720, 1278),
      _gradient(
        [0xFFCC2105, 0xFF0B0000],
        radial: true,
        center: const Offset(0, .14),
        radius: .84,
      ),
      opacity: _fire,
    ),
  );
  out.add(
    _draw(
      'fire_floor',
      '02 · Horizonte quente',
      44,
      70,
      (_) => _rect(0, 807, 720, 471),
      _gradient(
        [0xFF260604, 0xFF5A1F1A, 0xFFFBDAC6, 0xFFFFF2E7],
        stops: [0, .2, .61, 1],
      ),
      opacity: _fire,
    ),
  );

  // Each facade, opening and roof plane is separately editable.
  for (final red in [false, true]) {
    final start = red ? 44 : 0;
    final suffix = red ? 'hot' : 'cool';
    double visible(int q) => red ? _fire(q) : 1;
    BezierPath place(BezierPath p, int q) {
      final scale = _track([
        (0, .63),
        (6, .93),
        (18, 1.03),
        (43, 1.1),
        (69, .85),
      ], q);
      return _map(
        p,
        (v) => Offset(_houseX(q) + v.dx * scale, 816 + v.dy * scale),
        sx: scale,
        sy: scale,
      );
    }

    void part(
      String id,
      String label,
      String d,
      ShapeItem fill, {
      double glow = 0,
    }) {
      final p = _svg(d);
      out.add(
        _draw(
          'house_${id}_$suffix',
          '${red ? '02' : '01'} · $label',
          start,
          70,
          (q) => place(p, q),
          fill,
          opacity: visible,
          fx: glow == 0
              ? null
              : [
                  _glow(
                    red ? const Color(0xFFFF5934) : const Color(0xFF78FFD8),
                    25,
                    glow,
                  ),
                ],
        ),
      );
    }

    part(
      'side',
      'Fachada lateral',
      'M-145 0 L-145 -107 L-112 -185 L-64 -100 L-64 0 Z',
      _gradient(
        red ? [0xFF963014, 0xFF270805] : [0xFF0D4657, 0xFF07303A],
        angle: 20,
      ),
    );
    part(
      'wall',
      'Fachada principal',
      'M-64 -100 L64 -100 L64 0 L-64 0 Z',
      _gradient(
        red ? [0xFFEF734C, 0xFFBA2E12] : [0xFF08ED8F, 0xFF25A58B],
        angle: 5,
      ),
      glow: 4,
    );
    part(
      'roof',
      'Telhado em perspectiva',
      'M-112 -185 L32 -185 L78 -100 L-64 -100 Z',
      _gradient(
        red ? [0xFFFF724D, 0xFF8C1D0C] : [0xFF09F493, 0xFF246977],
        angle: 5,
      ),
      glow: 4,
    );
    part(
      'eave',
      'Beiral',
      'M-67 -102 L81 -102 L81 -96 L-67 -96 Z',
      ShapeFill(color: red ? const Color(0xFF61110A) : const Color(0xFF07414A)),
    );
    part(
      'chimney',
      'Chamine',
      'M-18 -197 L10 -194 L7 -159 L-14 -159 Z',
      ShapeFill(color: red ? const Color(0xFF72180A) : const Color(0xFF11424A)),
    );
    for (final (i, x) in [(0, -45.0), (1, 32.0)]) {
      part(
        'window$i',
        'Janela ${i + 1}',
        'M$x -67 L${x + 16} -67 L${x + 16} -35 L$x -35 Z',
        ShapeFill(
          color: red ? const Color(0xFF521007) : const Color(0xFF064536),
        ),
      );
    }
    part(
      'door',
      'Porta',
      'M-12 0 L-12 -65 L15 -65 L15 0 Z',
      ShapeFill(color: red ? const Color(0xFF40100B) : const Color(0xFF084834)),
    );
    for (final (i, x) in [(0, -130.0), (1, -95.0)]) {
      part(
        'slit$i',
        'Janela lateral ${i + 1}',
        'M$x -68 L${x + 3} -68 L${x + 3} -35 L$x -35 Z',
        ShapeFill(
          color: red ? const Color(0xFFFFB07F) : const Color(0xFF76FADB),
        ),
      );
    }
    part(
      'moon',
      'Abertura redonda',
      'M-105 -140 C-119 -138 -120 -121 -106 -117 C-114 -124 -113 -135 -105 -140 Z',
      ShapeFill(color: red ? const Color(0xFFFF875E) : const Color(0xFFA3F0E6)),
    );
    for (final (i, x) in [(0, -163.0), (1, 78.0)]) {
      part(
        'stone$i',
        'Pedra ${i + 1}',
        'M$x 8 L${x + 12} -4 L${x + 26} 9 Z',
        _gradient(
          red ? [0xFFFF9B69, 0xFF743028] : [0xFF42FFCF, 0xFF174955],
          angle: 10,
        ),
      );
    }
  }
  for (var i = 0; i < 4; i++) {
    out.add(
      _draw(
        'flame$i',
        '02 · Chama ${i + 1}',
        44,
        70,
        (q) {
          final h = 50 + 28 * math.sin(q * .9 + i * 1.7);
          final x = _houseX(q) - 80 + i * 25;
          return _svg(
            'M$x 636 Q${x - 12} ${600 - h * .25} ${x - 8} ${600 - h} Q${x + 32} 596 ${x + 13} 636 Z',
          );
        },
        _gradient([0xFF781000, 0xFFFF6022]),
        opacity: _fire,
      ),
    );
  }
  out.add(
    _draw(
      'pulse',
      '02 · Impactos de luz',
      44,
      62,
      (_) => _rect(0, 0, 720, 807),
      _gradient(
        [0xCC5BFFB4, 0x001AFF9A],
        radial: true,
        center: const Offset(0, .4),
      ),
      opacity: (q) => _track([
        (44, 1),
        (46, .1),
        (48, .95),
        (50, .1),
        (54, .6),
        (57, 0),
        (60, .2),
        (61, 0),
      ], q),
    ),
  );
  out.add(_spark(0, 44, false));
  return out;
}

ShapeLayer _spark(int start, int end, bool red) {
  final samples = referenceSparkSamples
      .where((s) => s[0] >= start && s[0] < end)
      .toList();
  List<(int, double)> track(int column) => [
    for (final s in samples) (s[0].toInt(), s[column]),
  ];
  final raw = _svg(
    'M0 -100 C2 -18 18 -2 100 0 C18 2 2 18 0 100 C-2 18 -18 2 -100 0 C-18 -2 -2 -18 0 -100 Z',
  );
  final shape = _draw(
    red ? 'eye_spark' : 'house_spark',
    red ? '03 · Estrela orbital' : '01 · Estrela orbital',
    start,
    end,
    (q) {
      final radius = q < 3
          ? _track([(0, 140), (3, 155)], q)
          : _track(track(3), q) * 2.02;
      final center = q == 0
          ? const Offset(-10, 1130)
          : Offset(_track(track(1), q), _track(track(2), q));
      return _map(
        raw,
        (p) => p * (radius / 100) + center,
        sx: radius / 100,
        sy: radius / 100,
      );
    },
    ShapeFill(color: red ? const Color(0xFFFFE0E0) : const Color(0xFFE4FFFF)),
    fx: [
      _glow(red ? const Color(0xFFFF193C) : const Color(0xFFAAEDFF), 20, 52),
    ],
  );
  // Unwrap the four-fold symmetry before creating rotation keyframes.
  double previous = 0;
  final angles = <double>[];
  for (var q = start; q < end; q++) {
    var angle = _track(track(4), q);
    while (angle - previous > 45) {
      angle -= 90;
    }
    while (angle - previous < -45) {
      angle += 90;
    }
    angles.add(angle);
    previous = angle;
  }
  return shape.copyLayer(
    rotation: _values(start, end, (q) => angles[q - start]),
  );
}

List<Layer> _eyeScene() {
  final out = <Layer>[];
  out.add(
    _static(
      'eye_bg',
      '03 · Fundo vinho',
      70,
      140,
      _rect(0, 0, 720, 1278),
      ShapeFill(color: const Color(0xFF0B0000)),
    ),
  );
  double width(int q) => _track([
    (70, 1250),
    (76, 970),
    (82, 680),
    (90, 590),
    (99, 650),
    (109, 510),
    (117, 465),
    (139, 505),
  ], q);
  double height(int q) => _track([
    (70, 800),
    (76, 190),
    (82, 260),
    (94, 305),
    (104, 295),
    (115, 210),
    (139, 238),
  ], q);
  BezierPath eye(int q, {double expand = 0}) {
    final w = width(q) + expand, h = height(q) + expand;
    return _svg(
      'M${360 - w / 2} 682 C${360 - w * .20} ${682 - h * .75} ${360 + w * .13} ${682 - h * .81} ${360 + w / 2} 677 C${360 + w * .22} ${682 + h * .55} ${360 - w * .30} ${682 + h * .65} ${360 - w / 2} 682 Z',
    );
  }

  out.add(
    _draw(
      'eye_halo',
      '03 · Halo vermelho',
      70,
      140,
      (q) => _ellipse(360, 685, width(q) * 1.55, height(q) * 2.65),
      _gradient([0xC8D90021, 0x001A0000], radial: true, radius: .60),
    ),
  );
  out.add(
    _draw(
      'eye_lid',
      '03 · Contorno da palpebra',
      70,
      140,
      (q) => eye(q, expand: 40),
      ShapeFill(color: const Color(0xFF2C1218)),
    ),
  );
  out.add(
    _draw(
      'eye_white',
      '03 · Branco do olho',
      70,
      140,
      eye,
      _gradient([0xFFFFE1DF, 0xFFFC889F], angle: 45),
      fx: [_glow(const Color(0xFFFF193C), 25, 28)],
    ),
  );
  out.add(
    _draw('iris', '03 · Iris seguindo a estrela', 74, 140, (q) {
      final x = _track([
        (74, 390),
        (84, 366),
        (96, 540),
        (102, 490),
        (113, 252),
        (119, 251),
        (130, 360),
        (139, 360),
      ], q);
      final y = _track([
        (74, 595),
        (84, 625),
        (96, 550),
        (102, 589),
        (113, 684),
        (119, 690),
        (130, 532),
        (139, 637),
      ], q);
      final r = _track([
        (74, 195),
        (84, 155),
        (96, 141),
        (114, 113),
        (139, 110),
      ], q);
      return _ellipse(x, y, r * 2, r * 2);
    }, _gradient([0xFF89182C, 0xFF2F1018], angle: 65)),
  );
  final irisMask = _draw(
    'iris_matte',
    '03 · Recorte da iris',
    74,
    140,
    eye,
    ShapeFill(color: const Color(0xFFFFFFFF)),
  );
  final irisIndex = out.indexWhere((l) => l.id == 'rebuild_iris');
  out[irisIndex] = out[irisIndex].copyLayer(
    matteMode: MatteMode.alpha,
    matteSourceId: irisMask.id,
  );
  out.add(irisMask);
  out.add(
    _draw(
      'tear',
      '03 · Lagrima deformavel',
      74,
      140,
      (q) {
        final x = 360 + width(q) * .37;
        final bottom = _track([
          (74, 1280),
          (84, 1040),
          (96, 1050),
          (115, 966),
          (139, 978),
        ], q);
        return _svg(
          'M${x - 95} 785 C${x - 85} 740 ${x + 40} 692 ${x + 77} 683 C${x + 25} 739 ${x + 2} 785 ${x + 2} 865 C${x + 2} ${bottom - 48} ${x + 34} ${bottom - 5} $x $bottom C${x - 49} ${bottom + 12} ${x - 13} ${bottom - 79} ${x - 15} 871 C${x - 19} 807 ${x - 26} 785 ${x - 95} 785 Z',
        );
      },
      _gradient([0xFFDD0028, 0xFF63000F, 0xFF310004], stops: [0, .45, 1]),
      fx: [_glow(const Color(0xFFFF0022), 22, 15)],
    ),
  );
  out.add(_spark(74, 140, true));
  out.add(
    _draw('eye_entry', '03 · Transicao pela estrela', 70, 77, (q) {
      final r = _track([(70, 3800), (72, 2000), (74, 850), (76, 440)], q);
      return _map(
        _svg(
          'M0 -100 C0 -18 18 0 100 0 C18 0 0 18 0 100 C0 18 -18 0 -100 0 C-18 0 0 -18 0 -100 Z',
        ),
        (p) => p * (r / 100) + const Offset(360, 620),
        sx: r / 100,
        sy: r / 100,
      );
    }, ShapeFill(color: const Color(0xFFFCE5E5))),
  );
  return out;
}

List<Layer> _swordScene() {
  final out = <Layer>[];
  out.add(
    _static(
      'sword_bg',
      '04 · Fundo violeta',
      140,
      208,
      _rect(0, 0, 720, 1278),
      ShapeFill(color: const Color(0xFF26203D)),
    ),
  );
  double guard(int q) => _track([
    (140, -300),
    (146, 125),
    (154, 240),
    (164, 280),
    (174, 296),
    (180, 370),
    (188, 460),
    (194, 705),
    (207, 742),
  ], q);
  double factor(int q) =>
      _track([(140, 1.12), (164, 1.1), (178, 1), (192, .78), (207, .67)], q);
  BezierPath place(BezierPath p, int q) => _map(
    p,
    (v) => Offset(360 + v.dx * factor(q), guard(q) + v.dy * factor(q)),
    sx: factor(q),
    sy: factor(q),
  );
  out.add(
    _draw(
      'sword_atmosphere',
      '04 · Luz ambiente',
      140,
      208,
      (q) => _ellipse(360, guard(q) + 330, 1150, 1050),
      _gradient([0xD07B79D9, 0x0026203D], radial: true, radius: .84),
    ),
  );
  out.add(
    _draw(
      'sword_floor',
      '04 · Solo',
      190,
      208,
      (q) => _rect(
        0,
        _track([(190, 1278), (195, 1092), (207, 1040)], q),
        720,
        400,
      ),
      _gradient([0xFFF68D63, 0xFF9B4A64, 0xFF26203D], stops: [0, .32, 1]),
    ),
  );
  final blade = _svg('M-90 20 L90 20 L85 520 L0 593 L-85 520 Z');
  out.add(
    _draw(
      'blade',
      '04 · Lamina',
      140,
      208,
      (q) => place(blade, q),
      _gradient([0xFF48406B, 0xFF7A668D, 0xFF8D5F81], angle: 90),
    ),
  );
  for (final (side, x) in [(0, -90.0), (1, 0.0)]) {
    out.add(
      _draw(
        'reflection$side',
        '04 · Reflexo face ${side + 1}',
        140,
        208,
        (q) {
          final sy = _track([
            (140, 400),
            (150, 580),
            (160, 30),
            (176, 230),
            (180, 225),
            (185, 390),
            (195, 330),
            (207, 285),
          ], q);
          return place(
            _svg(
              'M$x ${sy - 105 + side * 12} L${x + 90} ${sy - 150 + side * 12} L${x + 90} ${sy + 175 + side * 12} L$x ${sy + 220 + side * 12} Z',
            ),
            q,
          );
        },
        _gradient(
          [
            0x00745C8A,
            0xFFFF9669,
            0xFFFFFFFF,
            0xFFFFFFFF,
            0xFFFFAC7F,
            0x006E5585,
          ],
          angle: 64,
          stops: [0, .2, .38, .65, .82, 1],
        ),
        fx: [_glow(const Color(0xFFFFCAAD), 65, 110)],
      ),
    );
  }
  // Mascara local antes do glow: recorta o reflexo, mas deixa a luz
  // se espalhar para fora da lamina. Matte externo cortaria o halo.
  for (var side = 0; side < 2; side++) {
    final idx = out.indexWhere((l) => l.id == 'rebuild_reflection$side');
    final reflection = out[idx];
    final masks = [for (var q = 140; q < 208; q++)
      _map(place(blade, q), (p) => p - reflection.position.valueAt(
        referenceFrame(q) - referenceFrame(140)))];
    out[idx] = out[idx].copyLayer(
      masks: [LayerMask(name: 'Silhueta da lamina', path: AnimatedPath(masks.first, [
        for (var i = 0; i < masks.length; i++) Keyframe(
          time: referenceFrame(140 + i) - referenceFrame(140), value: masks[i]),
      ]))],
    );
  }
  out.add(
    _draw(
      'handle',
      '04 · Cabo',
      140,
      208,
      (q) => place(_svg('M-21 -360 L21 -360 L21 4 L-21 4 Z'), q),
      _gradient(
        [0xFFFFFFDD, 0xFFE4896A, 0xFF555889, 0xFF30263F],
        stops: [0, .27, .65, 1],
      ),
    ),
  );
  out.add(
    _draw(
      'guard',
      '04 · Guarda',
      140,
      208,
      (q) => place(
        _svg('M-153 -4 L157 -4 L157 43 L120 19 L-120 19 L-153 43 Z'),
        q,
      ),
      _gradient(
        [0xFF242242, 0xFFCA6C66, 0xFFFFC58E],
        angle: 0,
        stops: [0, .67, 1],
      ),
    ),
  );
  out.add(
    _draw(
      'pommel',
      '04 · Pomo',
      140,
      208,
      (q) => place(
        _svg(
          'M0 -434 C-2 -412 -27 -399 -25 -382 Q-22 -355 0 -355 Q22 -355 25 -382 C27 -399 2 -412 0 -434 Z',
        ),
        q,
      ),
      _gradient([0xFFF0A078, 0xFF4F365D]),
    ),
  );
  for (final side in [0, 1]) {
    final hand = _svg(
      'M-440 -257 C-320 -250 -209 -185 -71 -214 C-40 -225 -29 -260 -6 -254 C18 -251 46 -245 37 -228 C52 -223 49 -208 35 -203 C48 -196 42 -181 29 -179 C40 -167 33 -154 19 -154 C28 -142 17 -130 0 -132 C-85 -124 -210 -149 -440 -150 Z',
    );
    out.add(
      _draw(
        'hand$side',
        '04 · Mao ${side + 1}',
        186 + side * 3,
        208,
        (q) {
          final arrive = _track([
            (186 + side * 3, 520),
            (194 + side * 2, 0),
            (207, 0),
          ], q);
          final p = _map(
            hand,
            (v) => Offset(
              side == 0 ? v.dx - arrive : -v.dx + arrive,
              v.dy + side * 155,
            ),
            sx: side == 0 ? 1 : -1,
          );
          return place(p, q);
        },
        _gradient(
          [0x00494478, 0xFFE88C65, 0xFFFFFCE0, 0xFFFFFCF0],
          angle: side == 0 ? 0 : 180,
          stops: [0, .5, .8, 1],
        ),
        fx: [_glow(const Color(0xFFFFD7BA), 32, 45)],
      ),
    );
  }
  for (var i = 0; i < 9; i++) {
    out.add(
      _draw('crack$i', '04 · Rachadura ${i + 1}', 194, 208, (q) {
        final grow = _track([(194, .05), (201, .9), (207, 1.3)], q);
        final a = i * math.pi * 2 / 9;
        final root = Offset(360, guard(q) + factor(q) * 530);
        final pts = [
          Offset.zero,
          Offset(math.cos(a) * 36, math.sin(a) * 13),
          Offset(math.cos(a) * 52 + 10, math.sin(a) * 28),
          Offset(math.cos(a) * 85, math.sin(a) * 40),
          Offset(math.cos(a) * 121, math.sin(a) * 64),
        ];
        return BezierPath(
          closed: false,
          vertices: [for (final p in pts) PathVertex(p: root + p * grow)],
        );
      }, ShapeStroke(color: const Color(0xFF28203E), width: AnimatedDouble(4))),
    );
  }
  return out;
}

List<Layer> _crownsScene() {
  final out = <Layer>[
    _static(
      'crowns_bg',
      '05 · Fundo verde escuro',
      208,
      280,
      _rect(0, 0, 720, 1278),
      _gradient([0xFF343731, 0xFF111F13], radial: true, radius: 1.35),
    ),
  ];
  for (final (i, start, x, y, size) in [
    (0, 208, 360.0, 648.0, 185.0),
    (1, 228, 475.0, 287.0, 130.0),
    (2, 246, 241.0, 922.0, 100.0),
    (3, 266, 573.0, 980.0, 74.0),
  ]) {
    double zoom(int q) => _track([(208, 1), (269, 1), (279, 1.72)], q);
    double entrance(int q) =>
        _track([(start, .2), (start + 4, .9), (start + 8, 1)], q);
    // A coroa gira em seu proprio eixo, depois e inclinada. Converter
    // para a ordem XYZ do motor evita inclinar o eixo a cada volta.
    (double, double, double) orientation(int q) {
      final pitch =
          (i == 0
              ? _track([(208, 142), (214, 192), (225, 140), (279, 140)], q)
              : 210.0) *
          math.pi /
          180;
      final yaw = ((q - start) * 2.1 + i * 44) * math.pi / 180;
      final tilt = i == 0
          ? _track([
              (208, -20),
              (214, -30),
              (230, -8),
              (252, -24),
              (279, -10),
            ], q)
          : 20.0 + i * 4;
      final a = math.atan2(math.sin(pitch), math.cos(pitch) * math.cos(yaw));
      final b = math.asin((math.cos(pitch) * math.sin(yaw)).clamp(-1.0, 1.0));
      final c = math.atan2(math.sin(pitch) * math.sin(yaw), math.cos(yaw));
      return (a * 180 / math.pi, b * 180 / math.pi, c * 180 / math.pi + tilt);
    }

    out.add(
      _draw(
        'crown_halo$i',
        '05 · Halo coroa ${i + 1}',
        start,
        280,
        (q) => _ellipse(
          360 + (x - 360) * zoom(q),
          639 + (y - 639) * zoom(q),
          size * 3.7 * zoom(q) * entrance(q),
          size * 3.7 * zoom(q) * entrance(q),
        ),
        _gradient([0x44552A28, 0x00552A28], radial: true),
      ),
    );
    out.add(
      Element3DLayer(
        id: 'rebuild_crown$i',
        name: '05 · Coroa ${i + 1}',
        startTime: referenceFrame(start),
        duration: referenceFrame(280) - referenceFrame(start),
        kind: Element3DKind.crownFine,
        size: size,
        color: const Color(0xFFF74650),
        edges: false,
        reflect: .35,
        environment: EnvironmentKind.estudio,
        material: 1,
        shininess: .24,
        gradient: const [
          Color(0xFFFFF1F3),
          Color(0xFFFFCFD2),
          Color(0xFFF26665),
          Color(0xFFBD202F),
        ],
        position: AnimatedOffset(Offset(x, y), [
          for (var q = start; q < 280; q++)
            Keyframe(
              time: referenceFrame(q) - referenceFrame(start),
              value: Offset(
                360 + (x - 360) * zoom(q),
                639 + (y - 639) * zoom(q) + (1 - entrance(q)) * -170,
              ),
            ),
        ]),
        scaleX: _values(start, 280, (q) => zoom(q) * entrance(q)),
        scaleY: _values(start, 280, (q) => zoom(q) * entrance(q)),
        rotation: _values(start, 280, (q) => orientation(q).$3),
        rotationX: _values(start, 280, (q) => orientation(q).$1),
        rotationY: _values(start, 280, (q) => orientation(q).$2),
        effects: [_glow(const Color(0xFFFFA6A2), 12, 3)],
      ),
    );
  }
  return out;
}

VideoProject buildReferenceRebuildTemplate({String? audioPath}) {
  final bottomToTop = [
    ..._houseScene(),
    ..._eyeScene(),
    ..._swordScene(),
    ..._crownsScene(),
  ];
  final layers = <Layer>[...bottomToTop.reversed];
  if (audioPath != null) {
    layers.add(
      AudioLayer(
        id: 'rebuild_audio',
        name: 'Trilha da referencia',
        sourcePath: audioPath,
        startTime: Duration.zero,
        duration: referenceFrame(280),
      ),
    );
  }
  return VideoProject(
    name: 'Referencia · nova recriacao Codex',
    createdAt: DateTime.now(),
    aspectRatio: 720 / 1278,
    resolutionHeight: 720,
    fps: 30,
    layers: layers,
    meta: {
      for (final l in layers)
        l.id: LayerMeta(folder: l.name.split(' · ').first),
    },
    markers: [
      for (final (q, label) in [
        (0, 'Casa e orbita'),
        (44, 'Impactos'),
        (70, 'Olho'),
        (140, 'Espada'),
        (208, 'Coroas'),
      ])
        Marker(time: referenceFrame(q), label: label),
    ],
  );
}
