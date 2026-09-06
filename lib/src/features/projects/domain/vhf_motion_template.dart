import 'dart:math' as math;
import 'dart:ui';

import '../../editor/domain/effect.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/mask.dart';
import '../../editor/domain/shape.dart';
import '../../editor/domain/video_project.dart';
import 'vhf_motion_samples.dart';

/// Rebuilt vector choreography. No source MP4, screenshots or frame textures.
/// The source contains 234 frames at 24209/1000 fps; retain exact source PTS.
Duration vhfFrame(num q) =>
    Duration(microseconds: (q * 1000000000 / 24209).round());
const vhfFrameCount = 234;
const _black = 0xff08030d;
const _violet = 0xff351081;
const _pink = 0xfff92ea3;
const _red = 0xffff173d;
const _gold = 0xffffd48a;
const _white = 0xfff8f8f5;

double _lerp(List<(num, num)> keys, num q) {
  if (q <= keys.first.$1) return keys.first.$2.toDouble();
  for (var i = 1; i < keys.length; i++) {
    final a = keys[i - 1], b = keys[i];
    if (q <= b.$1) return a.$2 + (b.$2 - a.$2) * (q - a.$1) / (b.$1 - a.$1);
  }
  return keys.last.$2.toDouble();
}

AnimatedDouble _ad(int a, int b, double Function(int) f) =>
    AnimatedDouble(f(a), [
      for (var q = a; q < b; q++)
        Keyframe(time: vhfFrame(q) - vhfFrame(a), value: f(q)),
    ]);
BezierPath _map(BezierPath path, Offset Function(Offset) fn) => BezierPath(
  closed: path.closed,
  vertices: [
    for (final v in path.vertices)
      PathVertex(p: fn(v.p), inT: v.inT, outT: v.outT, corner: v.corner),
  ],
);
BezierPath _poly(List<Offset> points, {bool closed = true}) => BezierPath(
  closed: closed,
  vertices: [for (final p in points) PathVertex(p: p)],
);
BezierPath _rect(double x, double y, double w, double h) =>
    _map(BezierPath.rect(w, h), (p) => p + Offset(x + w / 2, y + h / 2));
BezierPath _oval(double x, double y, double w, double h) =>
    _map(BezierPath.ellipse(w, h), (p) => p + Offset(x, y));
BezierPath _pill(double x, double y, double w, double h, {double turn = 0}) {
  final p = BezierPath.roundedRect(w, h, math.min(w, h) / 2);
  final c = math.cos(turn), s = math.sin(turn);
  Offset rotate(Offset o) => Offset(o.dx * c - o.dy * s, o.dx * s + o.dy * c);
  return BezierPath(
    closed: true,
    vertices: [
      for (final v in p.vertices)
        PathVertex(
          p: rotate(v.p) + Offset(x, y),
          inT: rotate(v.inT),
          outT: rotate(v.outT),
        ),
    ],
  );
}

BezierPath _ring(
  double cx,
  double cy,
  double rx,
  double ry,
  double width, {
  double start = 0,
  double sweep = math.pi * 2,
  double turn = 0,
}) {
  Offset at(double t, double x, double y) {
    final a = x * math.cos(t), b = y * math.sin(t);
    return Offset(
      cx + a * math.cos(turn) - b * math.sin(turn),
      cy + a * math.sin(turn) + b * math.cos(turn),
    );
  }

  // A 20-degree stair sector does not need the tessellation of a full ring.
  final n = math.max(4, (64 * sweep.abs() / (2 * math.pi)).ceil());
  return _poly([
    for (var i = 0; i <= n; i++) at(start + sweep * i / n, rx, ry),
    for (var i = n; i >= 0; i--)
      at(
        start + sweep * i / n,
        math.max(.1, rx - width),
        math.max(.1, ry - width),
      ),
  ]);
}

ShapeGradientFill _gradient(
  List<int> colors, {
  double angle = 90,
  bool radial = false,
  Offset center = Offset.zero,
  double radius = 1,
  List<double>? stops,
  int? start,
  int? end,
  List<int> Function(int)? frames,
}) => ShapeGradientFill(
  colorA: Color(colors.first),
  colorB: Color(colors.last),
  extras: [for (final c in colors.skip(1).take(colors.length - 2)) Color(c)],
  angleDeg: angle,
  radial: radial,
  center: center,
  radiusScale: radius,
  stops: stops,
  colorFrames: frames == null
      ? null
      : [
          for (var q = start!; q < end!; q++)
            Keyframe(
              time: vhfFrame(q) - vhfFrame(start),
              value: [for (final c in frames(q)) Color(c)],
            ),
        ],
);
ShapeFill _fill(int c) => ShapeFill(color: Color(c));
EffectInstance _blur(double radius) => EffectInstance(
  type: EffectType.gaussianBlur,
  params: {'raio': AnimatedDouble(radius)},
);

ShapeLayer _draw(
  String id,
  String name,
  int a,
  int b,
  BezierPath Function(int) path,
  ShapeItem fill, {
  double Function(int)? opacity,
  List<EffectInstance>? effects,
}) {
  final paths = [for (var q = a; q < b; q++) path(q)];
  final centers = [for (final p in paths) p.build().getBounds().center];
  final locals = [
    for (var i = 0; i < paths.length; i++)
      _map(paths[i], (p) => p - centers[i]),
  ];
  bool samePath(BezierPath x, BezierPath y) {
    if (x.closed != y.closed || x.vertices.length != y.vertices.length) {
      return false;
    }
    for (var j = 0; j < x.vertices.length; j++) {
      final p = x.vertices[j], q = y.vertices[j];
      if ((p.p - q.p).distanceSquared > 1e-12 ||
          (p.inT - q.inT).distanceSquared > 1e-12 ||
          (p.outT - q.outT).distanceSquared > 1e-12) {
        return false;
      }
    }
    return true;
  }

  final staticPath = locals.every((p) => samePath(p, locals.first));
  final staticPosition = centers.every(
    (p) => (p - centers.first).distanceSquared < 1e-12,
  );
  return ShapeLayer(
    id: 'vhf_$id',
    name: name,
    startTime: vhfFrame(a),
    duration: vhfFrame(b) - vhfFrame(a),
    position: AnimatedOffset(centers.first, [
      if (!staticPosition)
        for (var i = 0; i < centers.length; i++)
          Keyframe(time: vhfFrame(a + i) - vhfFrame(a), value: centers[i]),
    ]),
    contents: [
      ShapeBezier(
        id: 'vhf_${id}_path',
        path: AnimatedPath(locals.first, [
          if (!staticPath)
            for (var i = 0; i < locals.length; i++)
              if (i == 0 ||
                  i == locals.length - 1 ||
                  !samePath(locals[i], locals[i - 1]) ||
                  !samePath(locals[i], locals[i + 1]))
                Keyframe(time: vhfFrame(a + i) - vhfFrame(a), value: locals[i]),
        ]),
      ),
      fill,
    ],
    effects: effects,
    opacity: opacity == null ? null : _ad(a, b, opacity),
  );
}

VideoProject buildVhfMotionTemplate({String? audioPath}) {
  // Build in paint order, then reverse once for the editor's top-first stack.
  final layers = <Layer>[];
  void add(
    String id,
    String name,
    int a,
    int b,
    BezierPath Function(int) path,
    ShapeItem fill, {
    double Function(int)? opacity,
    List<EffectInstance>? effects,
  }) {
    if (fill is ShapeGradientFill &&
        (id.startsWith('s05_facet') ||
            id == 's10_star' ||
            id == 's01_backring')) {
      layers.add(
        _draw(
          '${id}_bloom',
          '$name · reflexo difuso',
          a,
          b,
          path,
          fill,
          opacity: (_) => .35,
          effects: [_blur(22)],
        ),
      );
    }
    layers.add(
      _draw(id, name, a, b, path, fill, opacity: opacity, effects: effects),
    );
  }

  void light(
    String id,
    String name,
    int a,
    int b,
    BezierPath Function(int) path, {
    int color = _white,
    double blur = 16,
    double strength = .45,
  }) {
    add(
      '${id}_halo',
      '$name · halo',
      a,
      b,
      path,
      _fill(color),
      effects: [_blur(blur)],
      opacity: (_) => strength,
    );
    add(id, name, a, b, path, _fill(color));
  }

  void line(
    String id,
    int a,
    int b,
    List<Offset> Function(int) points, {
    int color = _white,
    double width = 2,
    double blur = 8,
  }) {
    final stroke = ShapeStroke(
      color: Color(color),
      width: AnimatedDouble(width),
      cap: StrokeCap.round,
    );
    add(
      '${id}_halo',
      'Rastro · brilho',
      a,
      b,
      (q) => _poly(points(q), closed: false),
      stroke,
      effects: [_blur(blur)],
      opacity: (_) => .55,
    );
    add(
      id,
      'Rastro · caminho editavel',
      a,
      b,
      (q) => _poly(points(q), closed: false),
      ShapeStroke(
        color: Color(color),
        width: AnimatedDouble(width),
        cap: StrokeCap.round,
      ),
    );
  }

  void background(String id, int a, int b, {bool horizontal = false}) {
    if ([0, 36, 106, 130, 178, 200, 212, 224].contains(a)) {
      add(
        id,
        'Fundo escuro',
        a,
        b,
        (_) => _rect(0, 0, 720, 1280),
        _fill(a == 0 || a >= 200 ? 0xff000000 : _black),
      );
      return;
    }
    final data = horizontal ? vhfHorizontalPalettes : vhfVerticalPalettes;
    add(
      id,
      'Fundo · cores animadas',
      a,
      b,
      (_) => _rect(0, 0, 720, 1280),
      _gradient(
        data[a],
        angle: horizontal ? -13 : 90,
        center: horizontal ? const Offset(0, -.38) : Offset.zero,
        start: a,
        end: b,
        frames: (q) => data[q],
      ),
    );
  }

  void orb(
    String id,
    int a,
    int b,
    double Function(int) x,
    double Function(int) y,
    double Function(int) w, {
    double Function(int)? h,
    int color = _white,
    double Function(int)? turn,
  }) {
    light(
      id,
      'Esfera · deformacao e percurso',
      a,
      b,
      (q) =>
          _pill(x(q), y(q), w(q), h?.call(q) ?? w(q), turn: turn?.call(q) ?? 0),
      color: color,
    );
  }

  void measuredOrb(String id, int a, int b) {
    List<double> bounds(int q) {
      final exact = vhfOrbBounds[q];
      if (exact != null) return exact;
      final nearest = vhfOrbBounds.keys.where((k) => k >= a && k < b).toList()
        ..sort((x, y) => (x - q).abs().compareTo((y - q).abs()));
      return nearest.isEmpty
          ? [360, 420, 100, 100]
          : vhfOrbBounds[nearest.first]!;
    }

    orb(
      id,
      a,
      b,
      (q) => bounds(q)[0],
      (q) => bounds(q)[1],
      (q) => bounds(q)[2],
      h: (q) => bounds(q)[3],
    );
  }

  void sphere(
    String id,
    int a,
    int b,
    double Function(int) x,
    double Function(int) y,
    double Function(int) r, {
    bool highlight = false,
    double angle = -35,
  }) {
    BezierPath path(int q) => _oval(x(q), y(q), 2 * r(q), 2 * r(q));
    add(
      '${id}_halo',
      'Esfera cromatica · aura',
      a,
      b,
      path,
      _gradient([_violet, _pink, _red, _gold], angle: angle),
      effects: [_blur(22)],
      opacity: (_) => .35,
    );
    add(
      id,
      'Esfera cromatica · superficie',
      a,
      b,
      path,
      _gradient(
        [
          0xff080115,
          0xff260c71,
          0xff68107e,
          0xffff0748,
          0xffff4d69,
          0xffffd99c,
        ],
        angle: angle,
        stops: [0, .16, .35, .65, .85, 1],
      ),
    );
    if (highlight) {
      add(
        '${id}_spec',
        'Esfera · reflexo',
        a,
        b,
        (q) => _pill(
          x(q) + r(q) * .58,
          y(q) - r(q) * .34,
          r(q) * .15,
          r(q) * .30,
          turn: -.45,
        ),
        _fill(_white),
      );
      add(
        '${id}_spec2',
        'Esfera · reflexo pequeno',
        a,
        b,
        (q) =>
            _oval(x(q) + r(q) * .7, y(q) - r(q) * .05, r(q) * .1, r(q) * .12),
        _fill(_white),
      );
    }
  }

  // 01 | Entrance: chromatic sphere, tilted rings and luminous trajectories.
  background('s01_bg', 0, 12);
  double r1(int q) =>
      _lerp([(0, 58), (2, 171), (4, 204), (6, 220), (8, 192), (11, 170)], q);
  double x1(int q) =>
      _lerp([(0, 369), (2, 240), (4, 209), (6, 215), (8, 269), (11, 371)], q);
  double y1(int q) =>
      _lerp([(0, 642), (2, 849), (4, 882), (6, 921), (8, 909), (11, 810)], q);
  add(
    's01_backring',
    '01 · Orbita externa',
    0,
    12,
    (q) => _ring(
      x1(q),
      y1(q) - 32,
      r1(q) * 1.9,
      r1(q) * 2.05,
      r1(q) * .62,
      turn: -.34,
    ),
    _gradient(
      [_gold, _pink, 0xff4d278c, _black],
      angle: 65,
      stops: [0, .16, .38, .7],
    ),
  );
  for (var i = 0; i < 3; i++) {
    line(
      's01_ray_$i',
      0,
      12,
      (q) => [
        Offset(x1(q) + r1(q) * .35, y1(q) - r1(q) * .25),
        Offset(
          _lerp([(0, 460), (3, 840), (6, 1000), (11, 430 + i * 290)], q),
          _lerp([(0, 600), (4, 230), (11, -50 + i * 120)], q),
        ),
      ],
      color: i == 0 ? _white : 0xffb7c7d0,
      width: i == 0 ? 4 : 1,
    );
  }
  add(
    's01_orbit',
    '01 · Arco em perspectiva',
    0,
    12,
    (q) => _ring(
      x1(q) + 100,
      y1(q) - 230,
      180 + q * 7,
      380,
      4,
      start: 3.4,
      sweep: 3.6,
      turn: .4 - q * .035,
    ),
    _gradient([0x007f8eb1, 0xffcfebff, 0x001e2440]),
  );
  sphere('s01_sphere', 0, 12, x1, y1, r1, angle: -40);
  add(
    's01_flash',
    '01 · Flash no ataque',
    6,
    7,
    (_) => _rect(0, 0, 720, 1280),
    _fill(_red),
    opacity: (_) => .5,
  );

  // 02 | Rolling over a large curved surface. Background bands change color.
  background('s02_bg', 12, 36, horizontal: true);
  double horizon(int q) =>
      _lerp([(12, 867), (18, 695), (24, 736), (30, 785), (35, 790)], q);
  double cx2(int q) => _lerp([(12, 1060), (22, 700), (35, 900)], q);
  double cy2(int q) => horizon(q) + 980;
  // Behind the foreground globe, the moving white orb and its trail.
  double ox2(int q) => _lerp([
    (12, -100),
    (14, 200),
    (16, 415),
    (18, 493),
    (20, 470),
    (22, 471),
    (24, 132),
    (26, 55),
    (28, 145),
    (30, 221),
    (32, 247),
    (35, 180),
  ], q);
  double oy2(int q) => _lerp([
    (12, 716),
    (14, 660),
    (16, 602),
    (18, 558),
    (20, 588),
    (22, 589),
    (24, 702),
    (26, 721),
    (28, 750),
    (30, 750),
    (32, 720),
    (35, 770),
  ], q);
  double or2(int q) => _lerp([
    (12, 180),
    (16, 200),
    (18, 165),
    (22, 121),
    (24, 102),
    (29, 93),
    (35, 91),
  ], q);
  line(
    's02_lightpath',
    12,
    36,
    (q) => [
      for (var j = 0; j <= 24; j++)
        Offset(ox2(q) - j * 13, oy2(q) + j * 12 + j * j * .6),
    ],
    width: 24,
    blur: 28,
  );
  sphere('s02_ball', 12, 36, ox2, oy2, or2, angle: 12);
  add(
    's02_balllight',
    '02 · Reflexo de contato',
    12,
    36,
    (q) => _oval(ox2(q), oy2(q), or2(q) * 2, or2(q) * 2),
    _gradient(
      [_white, _white, 0x00ffffff],
      radial: true,
      center: const Offset(-.15, -.1),
      radius: 1.7,
    ),
    opacity: (q) => q == 14 || q == 15 || q == 16 || q == 20 || q == 21
        ? 1
        : .8 + .2 * math.sin(q * 1.4),
  );
  add(
    's02_planet',
    '02 · Grande superficie curva',
    12,
    36,
    (q) => _oval(cx2(q), cy2(q), 2300, 2100),
    _gradient(
      [_pink, 0xff53083c, 0xff180b30, _black],
      angle: 70,
      stops: [0, .12, .3, 1],
    ),
  );
  add(
    's02_rim',
    '02 · Borda especular',
    12,
    36,
    (q) => _ring(cx2(q), cy2(q), 1151, 1051, 7),
    _gradient([_gold, _pink, 0xff402483, _black], angle: 70),
  );
  add(
    's02_arc',
    '02 · Faixa luminosa na superficie',
    14,
    36,
    (q) => _ring(cx2(q), cy2(q), 1138, 1038, 70, start: 3.1, sweep: 1.8),
    _gradient([_white, _pink, 0x00f90080], angle: 5),
    effects: [_blur(16)],
    opacity: (_) => .85,
  );

  // 03 | Tunnel and ellipse: independently editable concentric surfaces.
  background('s03_bg', 36, 60);
  add(
    's03_reardome',
    '03 · Cupula traseira',
    36,
    60,
    (q) => _oval(390, 1000, 540, 600),
    _gradient([_gold, _gold, _red, _black], angle: 90),
  );
  add(
    's03_centerfill',
    '03 · Reflexo interno do tunel',
    36,
    60,
    (q) => _oval(480, 1240, 600, 610),
    _gradient([_gold, _gold, 0xffae7193, _black], angle: 90),
  );
  for (var k = 0; k < 5; k++) {
    add(
      's03_floor_$k',
      '03 · Arco do tunel ${k + 1}',
      36,
      60,
      (q) {
        final rx = 760 - k * 100.0 + (q - 36) * 2.1, ry = 540 - k * 67.0;
        final cx = _lerp([(36, 614), (44, 365), (59, 360)], q);
        return _ring(
          cx,
          1350 - k * 29,
          rx,
          ry,
          95,
          start: math.pi,
          sweep: math.pi,
        );
      },
      _gradient(
        k.isEven
            ? [_violet, 0xff8f62d4, _gold, 0xff260825]
            : [_black, _red, _pink, _gold],
        angle: 80,
      ),
    );
  }
  add(
    's03_overhead',
    '03 · Arco superior',
    36,
    60,
    (q) => _ring(100, -850 + (q - 36) * 5, 1200, 980, 80),
    _gradient([_black, _violet, 0xff4d2491]),
  );
  double ringX(int q) => _lerp([(36, 366), (43, 342), (49, 321), (59, 433)], q);
  double ringY(int q) => _lerp([(36, 535), (45, 576), (50, 605), (59, 677)], q);
  add(
    's03_loopglow',
    '03 · Orbita vertical · halo',
    36,
    60,
    (q) => _ring(
      ringX(q),
      ringY(q),
      _lerp([(36, 108), (48, 116), (59, 62)], q),
      210,
      42,
      turn: _lerp([(36, -.6), (43, -.2), (59, .3)], q),
    ),
    _gradient([_red, _pink, _violet, 0xffabc3ff], angle: 60),
    effects: [_blur(20)],
  );
  add(
    's03_loop',
    '03 · Orbita vertical',
    36,
    60,
    (q) => _ring(
      ringX(q),
      ringY(q),
      _lerp([(36, 108), (48, 116), (59, 62)], q),
      210,
      31,
      turn: _lerp([(36, -.6), (43, -.2), (59, .3)], q),
    ),
    _gradient([_red, _pink, _violet, 0xffabc3ff], angle: 60),
  );
  light(
    's03_loopflash',
    '03 · Orbita branca de expansao',
    46,
    51,
    (q) => _ring(345, 680, 90 + (q - 46) * 52, 280 + (q - 46) * 110, 9),
    blur: 24,
    strength: .3,
  );
  orb(
    's03_point',
    36,
    48,
    (q) => _lerp([(36, 330), (40, 349), (47, 407)], q),
    (q) => _lerp([(36, 755), (40, 550), (47, 602)], q),
    (q) => _lerp([(36, 70), (47, 43)], q),
    color: 0xffffd7fc,
  );
  orb(
    's03_exit',
    48,
    60,
    (q) => _lerp([(48, 421), (51, 567), (54, 493), (58, 462), (59, 418)], q),
    (q) => _lerp([(48, 558), (53, 509), (58, 531), (59, 570)], q),
    (_) => 41,
    h: (q) => _lerp([(48, 48), (52, 50), (56, 90), (59, 121)], q),
    turn: (q) => (q - 48) * .16,
  );

  // 04 | A luminous capsule traverses an expanding striped runway.
  background('s04_bg', 60, 86);
  for (var k = 0; k < 8; k++) {
    add(
      's04_strip_$k',
      '04 · Faixa de pista ${k + 1}',
      60,
      86,
      (q) {
        final horizon = _lerp([(60, 1180), (70, 850), (78, 840), (85, 975)], q);
        final step = 72.0;
        final top = (k - 3.5) * step + 360;
        return _poly([
          Offset(top, horizon),
          Offset(top + step * .9, horizon),
          Offset(top + step * 1.8 + (k - 3.5) * 80, 1350),
          Offset(top + (k - 3.5) * 80, 1350),
        ]);
      },
      _gradient(
        k.isEven ? [0xff677ec2, 0xffb5d6ec, _violet] : [_gold, _pink, _red],
        angle: 90,
      ),
    );
  }
  measuredOrb('s04_jump', 60, 72);
  add('s04_curtain', '04 · Feixe triangular', 72, 86, (q) {
    final left = _lerp([(72, 400), (76, 70), (80, -130), (85, -800)], q);
    final right = _lerp([(72, 730), (76, 730), (80, 850), (85, 1500)], q);
    return _poly([
      Offset(left, -100),
      Offset(right, -100),
      Offset(670 - (q - 72) * 16, 1350),
    ]);
  }, _gradient([_red, _pink, _gold, _white], angle: 90));
  orb(
    's04_dark',
    72,
    86,
    (q) => _lerp([(72, 432), (74, 380), (78, 371), (82, 370), (85, 360)], q),
    (q) => _lerp([(72, 180), (76, 219), (80, 442), (84, 936), (85, 1100)], q),
    (q) => _lerp([(72, 120), (84, 129)], q),
    h: (q) => _lerp([(72, 132), (78, 194), (82, 426), (84, 267), (85, 70)], q),
    color: 0xff100414,
  );

  // 05 | A rotating, four-facet prism and an independently tracked sphere.
  background('s05_bg', 86, 106);
  Offset tip(int q) => Offset(
    _lerp([
      (86, 280),
      (90, 276),
      (94, 269),
      (98, 262),
      (101, 411),
      (104, 403),
      (105, 334),
    ], q),
    _lerp([
      (86, 1030),
      (90, 660),
      (94, 376),
      (98, 510),
      (101, 580),
      (104, 806),
      (105, 963),
    ], q),
  );
  Offset base(int q) => Offset(
    _lerp([(86, 40), (94, -360), (100, -600), (104, -200), (105, -300)], q),
    _lerp([(86, 1440), (94, 1500), (100, 900), (104, -400), (105, -600)], q),
  );
  for (var k = 0; k < 4; k++) {
    add(
      's05_facet_$k',
      '05 · Prisma / face ${k + 1}',
      86,
      106,
      (q) {
        final p = tip(q), b = base(q), v = b - p;
        final n = Offset(-v.dy, v.dx) / math.max(1, v.distance) * 1050;
        return _poly([p, b + n * (k / 4 - .5), b + n * ((k + 1) / 4 - .5)]);
      },
      _gradient(
        k.isEven ? [_gold, _pink, _red] : [0xffa3bcef, _pink, 0xffff1350],
        angle: 45,
      ),
    );
    line('s05_edge_$k', 86, 106, (q) {
      final p = tip(q), b = base(q), v = b - p;
      final n = Offset(-v.dy, v.dx) / math.max(1, v.distance) * 1050;
      return [p, b + n * (k / 4 - .5)];
    }, width: 2);
  }
  measuredOrb('s05_sphere', 86, 106);

  // 06 | Two orbiting spheres, curved trails and a pulsating central pillar.
  background('s06_bg', 106, 130);
  add(
    's06_burst',
    '06 · Explosao cromatica',
    118,
    128,
    (_) => _rect(0, 0, 720, 1280),
    _gradient(
      [_white, _gold, _pink, _red, _gold, _red],
      radial: true,
      radius: 1.45,
      stops: [0, .1, .28, .54, .82, 1],
    ),
  );
  for (var k = 0; k < 12; k++) {
    add(
      's06_burst_ray_$k',
      '06 · Feixe radial ${k + 1}',
      118,
      128,
      (q) {
        final a = k * math.pi / 6 + (q - 118) * .025;
        return _poly([
          const Offset(360, 650),
          Offset(360 + math.cos(a) * 1500, 650 + math.sin(a) * 1500),
          Offset(
            360 + math.cos(a + .14) * 1500,
            650 + math.sin(a + .14) * 1500,
          ),
        ]);
      },
      _gradient([_gold, 0x00ffbd8b], angle: k * 30.0),
      effects: [_blur(12)],
      opacity: (_) => .55,
    );
  }
  add(
    's06_pillar',
    '06 · Coluna cromatica',
    106,
    130,
    (q) => _rect(
      _lerp([(106, 199), (114, 300), (120, 325), (129, 261)], q),
      _lerp([(106, 970), (112, 356), (118, -80), (129, -100)], q),
      _lerp([(106, 319), (114, 108), (122, 46), (129, 165)], q),
      1600,
    ),
    _gradient([
      0xff171127,
      0xffa4caee,
      _violet,
      _pink,
      _red,
      _gold,
      _white,
    ], angle: 90),
  );
  double ax(int q) => _lerp([
    (106, 280),
    (108, 167),
    (112, 121),
    (116, 214),
    (118, 300),
    (121, 160),
    (124, 243),
    (127, 445),
    (129, 648),
  ], q);
  double ay(int q) => _lerp([
    (106, 160),
    (110, 301),
    (115, 572),
    (118, 535),
    (121, 869),
    (124, 974),
    (127, 1052),
    (129, 1201),
  ], q);
  double bx(int q) => _lerp([
    (106, 389),
    (110, 567),
    (114, 603),
    (118, 341),
    (122, 389),
    (126, 565),
    (129, 355),
  ], q);
  double by(int q) => _lerp([
    (106, 65),
    (111, 251),
    (116, 445),
    (119, 581),
    (123, 852),
    (127, 1062),
    (129, 1300),
  ], q);
  for (var k = 0; k < 4; k++) {
    line(
      's06_trail_$k',
      106,
      130,
      (q) => [
        for (var i = 0; i <= 40; i++)
          Offset(
            (k.isEven ? ax(q) : bx(q)) + math.sin(i * .12 + k * .4) * (i * 8),
            (k.isEven ? ay(q) : by(q)) - i * 18,
          ),
      ],
      color: k.isEven ? _gold : 0xffe9dafa,
      width: k == 0 ? 6 : 2,
      blur: 12,
    );
  }
  orb('s06_orb_a', 106, 118, ax, ay, (q) => _lerp([(106, 80), (117, 126)], q));
  orb('s06_orb_b', 106, 118, bx, by, (q) => _lerp([(106, 84), (117, 62)], q));
  orb(
    's06_orb_a_dark',
    118,
    130,
    ax,
    ay,
    (q) => _lerp([(118, 96), (129, 140)], q),
    color: 0xff110917,
    h: (q) => _lerp([(118, 116), (126, 104), (129, 80)], q),
    turn: (q) => (q - 118) * .06,
  );
  orb('s06_orb_b_dark', 118, 130, bx, by, (_) => 61, color: 0xff100719);
  light(
    's06_pillar_edge',
    '06 · Luz da coluna',
    118,
    130,
    (q) => _rect(_lerp([(118, 309), (124, 329), (129, 275)], q), -20, 5, 1350),
    blur: 12,
    strength: .7,
  );

  // 07 | Concentric planetary bands, metallic lip and falling sphere.
  background('s07_bg', 130, 152);
  double sy7(int q) => _lerp([
    (130, -283),
    (136, -215),
    (142, -100),
    (147, -113),
    (151, -357),
  ], q);
  double sr7(int q) =>
      _lerp([(130, 709), (136, 780), (143, 840), (148, 833), (151, 660)], q);
  add(
    's07_disk',
    '07 · Grande disco em gradiente',
    130,
    152,
    (q) => _oval(360, sy7(q) + 200, sr7(q) * 2, sr7(q) * 2),
    _gradient(
      [_violet, _red, _red, _gold, _white],
      radial: true,
      center: const Offset(0, -.05),
      stops: [0, .42, .67, .94, 1],
    ),
  );
  add(
    's07_inner',
    '07 · Disco interno',
    130,
    152,
    (q) => _oval(360, sy7(q) + 25, sr7(q) * 1.44, sr7(q) * 1.44),
    _gradient(
      [_violet, 0xff8b126d, _red, _gold],
      radial: true,
      center: const Offset(0, .12),
      radius: .76,
      stops: [0, .4, .75, 1],
    ),
  );
  add(
    's07_chrome',
    '07 · Borda metalica',
    130,
    152,
    (q) => _ring(
      360,
      sy7(q) + 200,
      sr7(q) + 7,
      sr7(q) + 7,
      42,
      start: 0,
      sweep: math.pi,
    ),
    _gradient([
      _white,
      0xffcbbccc,
      0xff45374f,
      _white,
      _pink,
      _black,
      _white,
      _gold,
      _white,
      0xff242630,
      _white,
    ], angle: 0),
  );
  orb(
    's07_ball',
    130,
    144,
    (_) => 360,
    (q) =>
        _lerp([(130, 390), (136, 254), (140, 367), (142, 547), (143, 660)], q),
    (q) => _lerp([(130, 80), (142, 82)], q),
    h: (q) => _lerp([(130, 100), (140, 117), (142, 220), (143, 180)], q),
    color: 0xff201225,
  );
  orb(
    's07_flashball',
    144,
    148,
    (_) => 360,
    (q) => _lerp([(144, 822), (146, 603), (147, 560)], q),
    (_) => 86,
    h: (_) => 92,
    color: 0xffffedb4,
  );
  orb(
    's07_return',
    148,
    152,
    (_) => 360,
    (q) => _lerp([(148, 491), (150, 342), (151, 290)], q),
    (_) => 82,
    h: (_) => 100,
    color: 0xff200d2a,
  );

  // 08 | Bounces on keys, squash/stretch and geometric accent particles.
  background('s08_bg', 152, 178);
  add(
    's08_floor',
    '08 · Piso reflexivo',
    152,
    178,
    (q) => _rect(
      0,
      _lerp([(152, 1200), (158, 1160), (166, 941), (177, 972)], q),
      720,
      600,
    ),
    _gradient(
      [_black, _red, _gold, _pink, 0xff8a1656],
      angle: 90,
      stops: [0, .08, .25, .36, 1],
    ),
  );
  for (var k = 0; k < 7; k++) {
    line(
      's08_floorline_$k',
      166,
      178,
      (q) => [
        Offset(0, 1010 + k * 38.0 + (q - 166) * .6),
        Offset(720, 1010 + k * 38.0 + (q - 166) * .6),
      ],
      color: 0xffcfa1bf,
      width: 1,
      blur: 1,
    );
  }
  add('s08_key', '08 · Tecla e impacto', 152, 178, (q) {
    final w = _lerp([
      (152, 406),
      (156, 380),
      (162, 145),
      (166, 160),
      (177, 179),
    ], q);
    final h = _lerp([
      (152, 780),
      (154, 355),
      (158, 95),
      (162, 25),
      (177, 42),
    ], q);
    final y = _lerp([
      (152, 152),
      (154, 840),
      (158, 1075),
      (162, 970),
      (166, 925),
      (177, 916),
    ], q);
    return _rect(360 - w / 2, y, w, h);
  }, _gradient([0xffd33747, _white, _red, 0xffa70725], angle: 60));
  measuredOrb('s08_sphere', 152, 166);
  measuredOrb('s08_small', 168, 178);
  add(
    's08_flash',
    '08 · Inversao de luz',
    166,
    168,
    (_) => _rect(0, 0, 720, 1280),
    _gradient([_white, 0xffede5ff, _violet, _pink], angle: 90),
  );
  orb(
    's08_dark',
    166,
    168,
    (_) => 296,
    (_) => 656,
    (_) => 64,
    h: (_) => 87,
    color: _black,
    turn: (_) => -.4,
  );
  for (var k = 0; k < 8; k++) {
    add('s08_particle_$k', '08 · Particula geometrica ${k + 1}', 166, 178, (q) {
      final t = (q - 166) / 12, ang = k * .88 + t * .22;
      final c = Offset(330 + math.cos(ang) * 150, 690 + math.sin(ang) * 135);
      if (k % 3 == 0) return _oval(c.dx, c.dy, 48 - k * 2.0, 48 - k * 2.0);
      final n = k % 3 == 1 ? 3 : 4;
      return _poly([
        for (var j = 0; j < n; j++)
          c +
              Offset(
                    math.cos(ang + j * math.pi * 2 / n),
                    math.sin(ang + j * math.pi * 2 / n),
                  ) *
                  (20.0 - k),
      ]);
    }, ShapeStroke(color: const Color(0xffa89fc6), width: AnimatedDouble(1.3)));
  }

  // 09 | Rotating radial stairs: every sector is a native Bezier shape.
  background('s09_bg', 178, 200);
  double wheelX(int q) =>
      _lerp([(178, 360), (182, 485), (187, 340), (192, 485), (199, 365)], q);
  double wheelY(int q) =>
      _lerp([(178, 647), (182, 660), (187, 712), (192, 640), (199, 631)], q);
  double wheelR(int q) =>
      _lerp([(178, 568), (181, 575), (186, 728), (192, 700), (199, 475)], q);
  double wheelTurn(int q) => _lerp([
    (178, -.7),
    (184, -1.4),
    (190, -2.1),
    (196, -3.7),
    (199, -4.4),
  ], q);
  for (var band = 1; band >= 0; band--) {
    for (var k = 0; k < 18; k++) {
      final index = band * 18 + k;
      BezierPath wedge(int q) {
        final r = wheelR(q) * (band == 1 ? 1 : .59),
            w = r * (band == 1 ? .42 : 1);
        return _ring(
          wheelX(q),
          wheelY(q),
          r,
          r,
          w,
          start: wheelTurn(q) + k * math.pi / 9,
          sweep: math.pi / 9 - .007,
        );
      }

      add(
        's09_wedge_$index',
        '09 · Degrau radial ${index + 1}',
        178,
        200,
        wedge,
        _gradient([_black, 0xff020105, 0xff15121b], angle: k * 20.0),
      );
      add(
        's09_line_$index',
        '09 · Contorno radial ${index + 1}',
        178,
        200,
        wedge,
        ShapeStroke(color: const Color(0xffc7cedc), width: AnimatedDouble(2.0)),
      );
      {
        add(
          's09_lit_$index',
          '09 · Acento luminoso ${index + 1}',
          178,
          200,
          wedge,
          ShapeStroke(
            color: Color(k.isEven ? _pink : _white),
            width: AnimatedDouble(9),
          ),
          opacity: (q) {
            if (q >= 188 && q < 192) return 0;
            final angle = _lerp([
              (178, 1.9),
              (184, -.3),
              (190, -2.9),
              (199, -6.3),
            ], q);
            final d = math.sin((k + .5) * math.pi / 9 + wheelTurn(q) - angle);
            return .95 * math.exp(-d * d / .07);
          },
        );
      }
    }
  }
  orb(
    's09_ball',
    178,
    200,
    (q) {
      final r = _lerp([
        (178, 300),
        (184, 314),
        (190, 110),
        (194, 31),
        (199, 0),
      ], q);
      final a = _lerp([(178, 1.9), (184, -.3), (190, -2.9), (199, -6.3)], q);
      return wheelX(q) + math.cos(a) * r;
    },
    (q) {
      final r = _lerp([
        (178, 300),
        (184, 314),
        (190, 110),
        (194, 31),
        (199, 0),
      ], q);
      final a = _lerp([(178, 1.9), (184, -.3), (190, -2.9), (199, -6.3)], q);
      return wheelY(q) + math.sin(a) * r;
    },
    (q) => _lerp([(178, 80), (184, 60), (190, 40), (199, 75)], q),
    h: (q) => _lerp([(178, 175), (182, 66), (190, 35), (199, 75)], q),
    turn: (q) => wheelTurn(q) + 1,
  );
  add(
    's09_redflash',
    '09 · Flash vermelho',
    188,
    190,
    (_) => _rect(0, 0, 720, 1280),
    _fill(_red),
    opacity: (_) => .83,
  );
  add(
    's09_goldflash',
    '09 · Flash dourado',
    190,
    192,
    (_) => _rect(0, 0, 720, 1280),
    _fill(_gold),
    opacity: (_) => .83,
  );

  // 10 | Four concave wings, colored globe and separate specular accents.
  background('s10_bg', 200, 212);
  double starScale(int q) =>
      _lerp([(200, .1), (202, .85), (204, 1.18), (209, 1.1), (211, 1.25)], q);
  final star = <Offset>[];
  for (var k = 0; k < 4; k++) {
    final ang = k * math.pi / 2;
    for (var j = 0; j <= 28; j++) {
      final t = j / 28 * math.pi / 2;
      final p = Offset(650 * (1 - math.sin(t)), 650 * (1 - math.cos(t)));
      star.add(
        Offset(
          p.dx * math.cos(ang) - p.dy * math.sin(ang),
          p.dx * math.sin(ang) + p.dy * math.cos(ang),
        ),
      );
    }
  }
  add(
    's10_star',
    '10 · Superficie concava',
    200,
    212,
    (q) => _poly([
      for (final p in star) p * starScale(q) + const Offset(360, 650),
    ]),
    _gradient(
      [_violet, _red, _pink, _gold, _white, _violet, _black],
      radial: true,
      stops: [0, .24, .4, .54, .59, .78, 1],
    ),
  );
  double rx10(int q) =>
      _lerp([(200, 85), (202, 160), (204, 202), (209, 210), (211, 207)], q);
  double sx10(int q) =>
      _lerp([(200, 356), (202, 329), (208, 327), (211, 410)], q);
  double sy10(int q) =>
      _lerp([(200, 625), (202, 616), (208, 653), (211, 659)], q);
  add(
    's10_torus',
    '10 · Anel cromado',
    202,
    212,
    (q) => _ring(
      sx10(q) + 37,
      sy10(q) - 18,
      rx10(q) * 1.4,
      rx10(q) * 1.4,
      57,
      turn: .5,
    ),
    _gradient([_gold, _white, _pink, _red, _violet], angle: 65),
  );
  sphere('s10_globe', 200, 212, sx10, sy10, rx10, highlight: true, angle: -42);
  add(
    's10_rim',
    '10 · Aro fino da esfera',
    202,
    212,
    (q) => _ring(sx10(q), sy10(q), rx10(q) + 2, rx10(q) + 2, 6),
    _gradient([_gold, _white, _pink, _violet], angle: 45),
  );

  // 11 | Reflective rectangular platform and jumping orb.
  background('s11_bg', 212, 224);
  double platformY(int q) => _lerp([(212, 664), (216, 712), (223, 710)], q);
  for (var k = 0; k < 5; k++) {
    add(
      's11_panel_$k',
      '11 · Painel refletivo ${k + 1}',
      212,
      224,
      (q) => _rect(8 + k * 140.0, platformY(q), 139, 700),
      _gradient(
        k.isEven ? [_gold, _pink, _red] : [0xff86a2df, _pink, _red],
        angle: 90,
      ),
    );
  }
  line(
    's11_edge',
    212,
    224,
    (q) => [
      Offset(5, 1300),
      Offset(5, platformY(q)),
      Offset(715, platformY(q)),
      const Offset(715, 1300),
    ],
    width: 2,
  );
  measuredOrb('s11_ball', 212, 224);
  // 12 | Jagged, luminous prisms and the final shrinking portrait frame.
  background('s12_bg', 224, 234);
  add(
    's12_flash',
    '12 · Flash rosa',
    224,
    226,
    (_) => _rect(0, 0, 720, 1280),
    _fill(0xfff63bad),
  );
  add(
    's12_prev',
    '12 · Plataforma anterior',
    224,
    234,
    (q) => _rect(85 + (q - 224) * 8, 715, 360, 630),
    _gradient([0xffa6c3ea, _pink, _red], angle: 90),
  );
  final peaks = [
    const Offset(-180, 940),
    const Offset(510, 380),
    const Offset(680, 575),
    const Offset(135, 1050),
    const Offset(728, 1060),
  ];
  for (var k = 0; k < peaks.length; k++) {
    add(
      's12_peak_$k',
      '12 · Prisma final ${k + 1}',
      222,
      234,
      (q) {
        final progress = ((q - 222) / 5).clamp(0.0, 1.0);
        final point = Offset(
          peaks[k].dx,
          1380 + (peaks[k].dy - 1380) * progress + (q - 228) * 13,
        );
        return _poly([
          Offset(k * 135.0 - 100, 1450),
          point,
          Offset(k * 135 + 215, 1450),
        ]);
      },
      _gradient(
        k % 3 == 0 ? [_white, _white] : [_pink, _red, _gold],
        angle: 100,
      ),
    );
  }
  add('s12_frame', '12 · Moldura luminosa', 226, 232, (q) {
    final inset = _lerp([(226, 45), (228, 24), (231, 7)], q);
    final p = _rect(inset, 15, 720 - inset * 2, 1250);
    return p;
  }, ShapeStroke(color: const Color(_gold), width: AnimatedDouble(5)));
  measuredOrb('s12_ball', 226, 234);
  orb(
    's12_darkorb',
    224,
    226,
    (_) => 489,
    (_) => 403,
    (_) => 87,
    h: (_) => 119,
    color: 0xff260618,
  );

  if (audioPath != null) {
    layers.add(
      AudioLayer(
        id: 'vhf_audio',
        name: 'VHF · trilha de referencia',
        startTime: Duration.zero,
        duration: vhfFrame(234),
        sourcePath: audioPath,
      ),
    );
  }
  return VideoProject(
    id: 'vhf_neon_native_v1',
    name: 'VHF · Neon Orbit — Aurea',
    createdAt: DateTime(2026, 9, 5),
    aspectRatio: 9 / 16,
    resolutionHeight: 720,
    fps: 24,
    layers: layers.reversed.toList(),
    markers: [
      for (final e in <int, String>{
        0: '01 · Esfera',
        12: '02 · Superficie',
        36: '03 · Tunel',
        60: '04 · Pista',
        86: '05 · Prisma',
        106: '06 · Orbitas',
        130: '07 · Discos',
        152: '08 · Teclas',
        178: '09 · Roda',
        200: '10 · Reflexos',
        212: '11 · Plataforma',
        224: '12 · Final',
      }.entries)
        Marker(time: vhfFrame(e.key), label: e.value),
    ],
  );
}
