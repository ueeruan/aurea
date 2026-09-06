import 'dart:math' as math;
import 'dart:ui';

import '../../editor/domain/effect.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/layer_meta.dart';
import '../../editor/domain/mask.dart';
import '../../editor/domain/shape.dart';
import '../../editor/domain/shape_library.dart';
import '../../editor/domain/text_anim.dart';
import '../../editor/domain/video_project.dart';
import 'dnyx_pixel_geometry.dart';

/// Source clock: 309 frames at 2997/100 fps. Keyframes retain source PTS.
Duration dnyxFrame(num q) =>
    Duration(microseconds: (q * 100000000 / 2997).round());
const dnyxAssetNames = [
  'portrait.png',
  'gallery-center.png',
  'gallery-left.png',
  'gallery-right.png',
  'reaction-face.png',
  'reaction-fire.png',
  'reaction-clap.png',
  'audio.m4a',
];

double _at(List<(int, double)> keys, int q) {
  if (q <= keys.first.$1) return keys.first.$2;
  for (var i = 1; i < keys.length; i++) {
    final a = keys[i - 1], b = keys[i];
    if (q <= b.$1) return a.$2 + (b.$2 - a.$2) * (q - a.$1) / (b.$1 - a.$1);
  }
  return keys.last.$2;
}

AnimatedDouble _a(int a, int b, double Function(int) f) =>
    AnimatedDouble(f(a), [
      for (var q = a; q < b; q++)
        Keyframe(time: dnyxFrame(q) - dnyxFrame(a), value: f(q)),
    ]);
AnimatedOffset _p(int a, int b, Offset Function(int) f) =>
    AnimatedOffset(f(a), [
      for (var q = a; q < b; q++)
        Keyframe(time: dnyxFrame(q) - dnyxFrame(a), value: f(q)),
    ]);
EffectInstance _blur(int a, int b, double Function(int) f) => EffectInstance(
  type: EffectType.gaussianBlur,
  params: {'raio': _a(a, b, f)},
);
EffectInstance _glow(Color color, double radius, double intensity) =>
    EffectInstance(
      type: EffectType.lightGlow,
      color: color,
      params: {
        'raio': AnimatedDouble(radius),
        'intensity': AnimatedDouble(intensity),
        'threshold': AnimatedDouble(35),
        'piramide': AnimatedDouble(3),
        'mult_r': AnimatedDouble(1),
        'mult_g': AnimatedDouble(1),
        'mult_b': AnimatedDouble(1),
        'mesclagem': AnimatedDouble(1),
      },
    );

ShapeLayer _shape(
  String id,
  String name,
  int a,
  int b,
  List<ShapeItem> items,
  Offset Function(int) pos, {
  double Function(int)? scale,
  double Function(int)? turn,
  double Function(int)? opacity,
  List<EffectInstance>? effects,
}) => ShapeLayer(
  id: 'dnyx_$id',
  name: name,
  startTime: dnyxFrame(a),
  duration: dnyxFrame(b) - dnyxFrame(a),
  contents: items,
  position: _p(a, b, pos),
  scaleX: scale == null ? null : _a(a, b, scale),
  scaleY: scale == null ? null : _a(a, b, scale),
  rotation: turn == null ? null : _a(a, b, turn),
  opacity: opacity == null ? null : _a(a, b, opacity),
  effects: effects,
);
ShapeLayer _rect(
  String id,
  String name,
  int a,
  int b,
  Offset Function(int) pos,
  double Function(int) w,
  double Function(int) h,
  Color color, {
  double radius = 0,
  double Function(int)? corner,
  double Function(int)? opacity,
  List<EffectInstance>? effects,
  Color? stroke,
  double strokeWidth = 1,
}) => _shape(
  id,
  name,
  a,
  b,
  [
    ShapeParametric(
      sizeX: _a(a, b, w),
      sizeY: _a(a, b, h),
      roundness: corner == null ? AnimatedDouble(radius) : _a(a, b, corner),
      roundnessPercent: false,
    ),
    ShapeFill(color: color),
    if (stroke != null)
      ShapeStroke(color: stroke, width: AnimatedDouble(strokeWidth)),
  ],
  pos,
  opacity: opacity,
  effects: effects,
);

TextLayer _text(
  String id,
  String name,
  String text,
  int a,
  int b,
  Offset Function(int) pos, {
  double size = 43,
  Color color = const Color(0xfff4f4f4),
  double Function(int)? scale,
  double Function(int)? opacity,
  List<EffectInstance>? effects,
  List<TextAnim>? anims,
}) => TextLayer(
  id: 'dnyx_$id',
  name: name,
  text: text,
  fontSize: size,
  fontFamily: 'Aurea Motion Sans',
  bold: false,
  color: color,
  startTime: dnyxFrame(a),
  duration: dnyxFrame(b) - dnyxFrame(a),
  position: _p(a, b, pos),
  scaleX: scale == null ? null : _a(a, b, scale),
  scaleY: scale == null ? null : _a(a, b, scale),
  opacity: opacity == null ? null : _a(a, b, opacity),
  effects: effects,
  anims: anims,
);

ImageLayer _image(
  String id,
  String name,
  String path,
  int a,
  int b,
  Offset Function(int) pos,
  double Function(int) width, {
  double aspect = 128 / 190,
  double radius = 18,
  double Function(int)? heightScale,
  double Function(int)? turn,
  double Function(int)? opacity,
  List<EffectInstance>? effects,
}) => ImageLayer(
  id: 'dnyx_$id',
  name: name,
  sourcePath: path,
  startTime: dnyxFrame(a),
  duration: dnyxFrame(b) - dnyxFrame(a),
  position: _p(a, b, pos),
  scaleX: _a(a, b, (q) => width(q) / 576),
  scaleY: _a(a, b, (q) => width(q) / 576 * (heightScale?.call(q) ?? 1)),
  rotation: turn == null ? null : _a(a, b, turn),
  opacity: opacity == null ? null : _a(a, b, opacity),
  // Effects run before the image's transform. Compensate the image scale
  // so the measured transition blur remains in output pixels.
  effects: effects == null
      ? null
      : [
          for (final e in effects)
            e.type == EffectType.gaussianBlur
                ? e.copyWith(
                    params: {
                      ...e.params,
                      'raio': _a(
                        a,
                        b,
                        (q) =>
                            e.paramAt('raio', dnyxFrame(q) - dnyxFrame(a)) *
                            1080 /
                            width(q).clamp(1, 1080),
                      ),
                    },
                  )
                : e,
        ],
  masks: radius <= 0
      ? null
      : [
          LayerMask(
            name: 'Cantos do cartao',
            path: AnimatedPath(
              BezierPath.roundedRect(576, 576 / aspect, radius * 576 / 154),
            ),
          ),
        ],
);

/// All graphics are authored shapes/text/keyframes. Only the photographs,
/// reactions and supplied soundtrack are isolated reference media assets.
/// No complete reference frame or source video is used in the project.
VideoProject buildDnyxRemixTemplate(Map<String, String> assets) {
  String asset(String name) =>
      assets[name] ?? (throw ArgumentError('Missing $name'));
  final layers = <Layer>[];
  void add(Layer l) => layers.add(l);
  const dark = Color(0xff110e13),
      paper = Color(0xffeceeeb),
      white = Color(0xfff6f6f6);
  const blue = Color(0xff1886e5), ink = Color(0xff030505);

  add(
    _rect(
      'dark',
      '01 · Fundo escuro',
      0,
      309,
      (_) => const Offset(288, 288),
      (_) => 576,
      (_) => 576,
      dark,
    ),
  );
  add(
    _rect(
      'blue_intro',
      '01 · Exposicao azul',
      0,
      10,
      (_) => const Offset(288, 288),
      (_) => 576,
      (_) => 576,
      const Color(0xff0d2247),
      opacity: (q) => 1 - q / 10,
    ),
  );
  // Frame-measured coloured rectangles, not an invented orbit of blocks.
  for (var kind = 0; kind < 3; kind++) {
    final maxCount = dnyxPixelGeometry
        .map((f) => f.where((r) => r[0] == kind).length)
        .reduce(math.max);
    for (var i = 0; i < maxCount; i++) {
      List<double>? row(int q) {
        final items = dnyxPixelGeometry[q].where((r) => r[0] == kind).toList();
        return i < items.length ? items[i] : null;
      }

      add(
        _rect(
          'tile_${kind}_$i',
          '01 · Pixel medido $kind-${i + 1}',
          0,
          24,
          (q) => Offset(row(q)?[1] ?? 0, row(q)?[2] ?? 0),
          (q) => row(q)?[3] ?? 1,
          (q) => row(q)?[4] ?? 1,
          [
            const Color(0xff397cf1),
            const Color(0xff449bc6),
            const Color(0xff58cfe5),
          ][kind],
          opacity: (q) => row(q) == null ? 0 : 1,
        ),
      );
    }
  }
  add(
    _shape(
      'orbit',
      '01 · Orbita',
      12,
      28,
      [
        ShapePath(primitive: ShapePrimitive.ellipse, width: 250, height: 250),
        ShapeStroke(color: const Color(0xff4e5065), width: AnimatedDouble(2.5)),
      ],
      (q) => Offset(_at([(12, 299), (18, 282), (25, 279)], q), 285),
      scale: (q) => _at([(12, .5), (18, 1), (27, 1.18)], q),
      opacity: (q) => _at([(12, 0), (17, .4), (27, 0)], q),
      effects: [_blur(12, 28, (_) => 2)],
    ),
  );
  add(
    _text(
      'lets',
      '01 · Lets',
      "Let's",
      3,
      66,
      (q) => Offset(
        _at([
          (3, 385),
          (8, 312),
          (12, 308),
          (18, 281),
          (22, 263),
          (26, 167),
          (32, 125),
          (41, 124),
          (45, 124),
          (60, 124),
          (65, 106),
        ], q),
        _at([
          (3, 432),
          (8, 427),
          (12, 398),
          (17, 300),
          (20, 288),
          (60, 288),
          (65, 283),
        ], q),
      ),
      size: 43,
      scale: (q) => _at([
        (3, .8),
        (8, 1),
        (14, .85),
        (19, 1),
        (41, 1),
        (60, 1.08),
        (65, 1.4),
      ], q),
      opacity: (q) => _at([(3, .1), (8, 1), (62, 1), (65, 0)], q),
      effects: [
        _blur(
          3,
          66,
          (q) => _at([(3, 8), (8, 2), (14, 3), (19, 0), (60, 0), (65, 12)], q),
        ),
      ],
    ),
  );
  add(
    _text(
      'get',
      '01 · Get',
      'get',
      24,
      66,
      (q) => Offset(
        _at([
          (24, 293),
          (31, 209),
          (41, 211),
          (45, 211),
          (60, 215),
          (65, 235),
        ], q),
        288,
      ),
      size: 43,
      opacity: (q) => _at([(24, .15), (30, 1), (62, 1), (65, 0)], q),
      effects: [
        _blur(24, 66, (q) => _at([(24, 8), (30, 0), (62, 0), (65, 9)], q)),
      ],
    ),
  );
  add(
    _text(
      'creating',
      '01 · Creating',
      'creating',
      27,
      66,
      (q) => Offset(
        _at([
          (27, 348),
          (32, 347),
          (41, 346),
          (45, 417),
          (60, 427),
          (65, 466),
        ], q),
        288,
      ),
      size: 43,
      scale: (q) => _at([(27, 1), (41, 1), (60, 1.08), (65, 1.4)], q),
      opacity: (q) => _at([(27, 0), (33, 1), (62, 1), (65, 0)], q),
      effects: [
        _blur(27, 66, (q) => _at([(27, 8), (33, 0), (62, 0), (65, 12)], q)),
      ],
    ),
  );
  // Selection highlight is behind the letters in the paint stack.
  final select = _rect(
    'selection',
    '01 · Selecao de texto',
    32,
    41,
    (q) => Offset(q < 35 ? 128 : 211, 287),
    (q) => q < 35 ? 89 : 72,
    (_) => 53,
    const Color(0xff405bbc),
  );
  layers.insert(12, select);
  for (var i = 0; i < 13; i++) {
    final x = math.Random(441 + i).nextDouble() * 565;
    final y = 440 + math.Random(892 + i).nextDouble() * 180;
    add(
      _shape(
        'particle_$i',
        '01 · Particula ${i + 1}',
        33,
        67,
        [
          ShapePath(
            primitive: ShapePrimitive.ellipse,
            width: 4 + (i % 4) * 2,
            height: 4 + (i % 4) * 2,
          ),
          ShapeFill(color: Color.fromARGB(125 + i % 3 * 55, 240, 244, 253)),
        ],
        (q) => Offset(
          x + (q - 45) * (i % 2 == 0 ? 1.2 : -.4),
          y - (q - 40) * (1.9 + i % 3),
        ),
        opacity: (q) => _at([(33, 0), (45, .8), (62, 1), (66, 0)], q),
        effects: [_glow(white, 5, 8)],
      ),
    );
  }
  double pw(int q) => _at([
    (43, 20),
    (49, 38),
    (56, 51),
    (61, 62),
    (65, 87),
    (68, 127),
    (72, 128),
    (80, 141),
    (84, 145),
    (88, 138),
    (94, 126),
    (96, 138),
    (101, 140),
    (106, 143),
    (110, 145),
    (112, 108),
    (114, 64),
    (132, 65),
    (139, 60),
    (142, 50),
  ], q);
  Offset photo(int q) => Offset(
    _at([
      (43, 290),
      (65, 292),
      (84, 292),
      (94, 286),
      (100, 300),
      (110, 303),
      (112, 302),
      (114, 344),
      (132, 319),
      (136, 245),
      (139, -90),
      (142, -600),
    ], q),
    _at([
      (43, 288),
      (65, 292),
      (72, 291),
      (80, 300),
      (86, 321),
      (90, 343),
      (94, 416),
      (96, 479),
      (100, 390),
      (104, 334),
      (110, 319),
      (114, 367),
      (132, 365),
      (139, 320),
      (142, 140),
    ], q),
  );
  add(
    _image(
      'portrait',
      '02 · Foto arrastavel',
      asset('portrait.png'),
      43,
      143,
      photo,
      pw,
      heightScale: (q) => _at([(43, 128/190), (60, 128/190), (65,.85), (68,1), (110, 1), (114, 128 / 190)], q),
      turn: (q) => _at([
        (43, 0),
        (62, 0),
        (68, 1),
        (87, 0),
        (97, -9),
        (108, -8),
        (114, 0),
      ], q),
      effects: [
        _glow(const Color(0xff2764f3), 22, 10),
        _blur(
          43,
          143,
          (q) => _at([
            (43, 1),
            (50, 0),
            (86, 0),
            (95, 9),
            (100, 2),
            (104, 0),
            (111, 0),
            (113, 3),
            (116, 0),
            (136, 0),
            (140, 8),
          ], q),
        ),
      ],
    ),
  );
  for (final (id, dx, dy, size, aspect) in [
    ('face', -73.0, 65.0, 58.0, 57 / 60),
    ('fire', 65.0, 92.0, 47.0, 50 / 60),
    ('clap', 81.0, -11.0, 46.0, 47 / 65),
  ]) {
    add(
      _image(
        'reaction_$id',
        '02 · Reacao $id',
        asset('reaction-$id.png'),
        66,
        98,
        (q) => photo(q) + Offset(dx, dy) * (pw(q) / 128),
        (q) =>
            size *
            pw(q) /
            128 *
            _at([(66, .1), (71, 1), (81, 1.08), (87, 1), (97, 1)], q),
        aspect: aspect,
        radius: 0,
        opacity: (q) => _at([(66, 0), (69, 1), (94, 1), (97, 0)], q),
        effects: [
          _blur(66, 98, (q) => _at([(66, 6), (72, 0), (88, 0), (97, 10)], q)),
        ],
      ),
    );
  }
  add(
    _shape(
      'drag_hand',
      '02 · Cursor mao arrastando',
      74,
      113,
      ShapeLibrary.cursorHand(),
      (q) => Offset(
        _at([
          (74, 290),
          (79, 282),
          (84, 287),
          (94, 286),
          (100, 293),
          (110, 289),
        ], q),
        _at([
          (74, 316),
          (79, 227),
          (84, 216),
          (90, 224),
          (96, 382),
          (100, 281),
          (106, 222),
          (110, 223),
        ], q),
      ),
      scale: (_) => .36,
    ),
  );

  // Whip to the light UI, inserted behind the photograph.
  final whiteWipe = _rect(
    'paper',
    '03 · Fundo claro',
    88,
    273,
    (q) => Offset(288, _at([(88, -300), (91, -220), (94, 5), (96, 288)], q)),
    (_) => 800,
    (_) => 800,
    paper,
    effects: [_blur(88, 273, (q) => q < 97 ? 23 : 0)],
  );
  layers.insert(2, whiteWipe);
  final dots = <ShapeItem>[];
  for (var y = -10; y < 15; y++) {
    for (var x = -15; x < 20; x++) {
      dots.add(
        ShapeBezier(
          path: AnimatedPath(
            BezierPath.ellipse(3, 3, center: Offset(x * 64, y * 64)),
          ),
        ),
      );
    }
  }
  dots.add(ShapeFill(color: const Color(0xffbfc6c2)));
  final grid = _shape(
    'grid',
    '03 · Grade de pontos',
    96,
    197,
    dots,
    (q) => Offset(
      _at([(96, 16), (132, 10), (144, -415), (176, -433), (195, -320)], q),
      _at([(96, 35), (132, 35), (144, -107), (176, -170), (195, -20)], q),
    ),
    opacity: (q) =>
        _at([(96, 0), (105, .25), (126, .5), (195, .6), (196, 0)], q),
  );
  layers.insert(3, grid);
  double right(int q) => _at([
    (96, 1190),
    (132, 1100),
    (136, 1060),
    (140, 820),
    (144, 528),
    (148, 465),
    (156, 443),
    (168, 424),
    (180, 415),
    (190, 416),
    (194, 446),
    (196, 459),
  ], q);
  double bw(int q) => q <= 136
      ? right(q) -
            _at([
              (96, 236),
              (103, 220),
              (113, 220),
              (120, 213),
              (132, 178),
              (136, 163),
            ], q)
      : _at([
          (96, 954),
          (132, 920),
          (136, 897),
          (140, 1500),
          (144, 1680),
          (156, 1700),
          (180, 1700),
          (190, 1450),
          (194, 1010),
          (196, 980),
        ], q);
  double bh(int q) => _at([
    (96, 134),
    (103, 213),
    (113, 239),
    (132, 239),
    (138, 270),
    (144, 664),
    (180, 742),
    (190, 530),
    (194, 450),
    (196, 430),
  ], q);
  double bottom(int q) => _at([
    (96, 337),
    (103, 410),
    (113, 414),
    (132, 410),
    (138, 375),
    (144, 405),
    (148, 408),
    (156, 399),
    (168, 418),
    (180, 417),
    (190, 450),
    (194, 476),
    (196, 488),
  ], q);
  Offset box(int q) => Offset(right(q) - bw(q) / 2, bottom(q) - bh(q) / 2);
  double corner(int q) => _at([
    (96, 40),
    (113, 55),
    (132, 55),
    (144, 104),
    (180, 115),
    (196, 92),
  ], q);
  double uiBlur(int q) => _at([
    (96, 6),
    (103, 0),
    (134, 0),
    (139, 10),
    (144, 0),
    (194, 0),
    (196, 16),
  ], q);
  final ui = <Layer>[
    _rect(
      'box_rim',
      '03 · Comando - aro branco',
      96,
      197,
      box,
      bw,
      bh,
      white,
      corner: corner,
      effects: [_blur(96, 197, uiBlur)],
    ),
    _rect(
      'box_bevel',
      '03 · Comando - rebaixo',
      96,
      197,
      box,
      (q) => bw(q) - 5,
      (q) => bh(q) - 5,
      const Color(0xffdfe2df),
      corner: (q) => corner(q) - 3,
      effects: [_blur(96, 197, uiBlur)],
    ),
    _rect(
      'box_face',
      '03 · Comando - superficie',
      96,
      197,
      box,
      (q) => bw(q) - 45,
      (q) => bh(q) - 45,
      white,
      corner: (q) => corner(q) - 20,
      effects: [_blur(96, 197, uiBlur)],
    ),
  ];
  double buttonInset(int q) => _at([
    (96, 125),
    (136, 125),
    (140, 132),
    (144, 163),
    (148, 132),
    (153, 125),
  ], q);
  // UI is behind the dragged photograph, not pasted over it.
  layers.insertAll(4, ui);
  double left(int q) => right(q) - bw(q);
  Offset typePos(int q, double dx, double dy) =>
      Offset(left(q) + dx, bottom(q) - bh(q) + dy);
  add(
    _text(
      'plus',
      '03 · Adicionar',
      '+',
      96,
      143,
      (q) => typePos(q, 66, 67),
      size: 48,
      color: ink,
      effects: [_blur(96, 143, uiBlur)],
    ),
  );
  add(
    _text(
      'prompt',
      '03 · Texto do comando',
      'Show me similar',
      96,
      143,
      (q) => typePos(q, 223, 60),
      size: 36,
      color: ink,
      anims: [
        TextAnim(
          specId: 'typewriter',
          slot: TextAnimSlot.entrada,
          stagger: dnyxFrame(1.45),
        ),
      ],
      effects: [_blur(96, 143, uiBlur)],
    ),
  );
  add(
    _text(
      'prompt_second',
      '03 · Segunda linha',
      'photos',
      117,
      143,
      (q) => typePos(q, 149, 105),
      size: 36,
      color: ink,
      anims: [
        TextAnim(
          specId: 'typewriter',
          slot: TextAnimSlot.entrada,
          stagger: dnyxFrame(1.2),
        ),
      ],
      effects: [_blur(117, 143, uiBlur)],
    ),
  );
  add(
    _shape(
      'send_button',
      '03 · Botao enviar',
      96,
      197,
      [
        ShapePath(primitive: ShapePrimitive.ellipse, width: 84, height: 84),
        ShapeFill(color: ink),
      ],
      (q) => Offset(right(q) - buttonInset(q), bottom(q) - 120),
      scale: (q) => _at([
        (96, .7),
        (136, .7),
        (145, 1),
        (174, 1),
        (178, .93),
        (182, 1),
        (190, .8),
        (196, .65),
      ], q),
      effects: [_blur(96, 197, uiBlur)],
    ),
  );
  add(
    _shape(
      'send_arrow',
      '03 · Seta enviar',
      96,
      197,
      [
        ShapeBezier(
          path: AnimatedPath(
            BezierPath(
              closed: false,
              vertices: [
                const PathVertex(p: Offset(-13, -2)),
                const PathVertex(p: Offset(0, -15)),
                const PathVertex(p: Offset(13, -2)),
              ],
            ),
          ),
        ),
        ShapeBezier(
          path: AnimatedPath(
            BezierPath(
              closed: false,
              vertices: [
                const PathVertex(p: Offset(0, -15)),
                const PathVertex(p: Offset(0, 17)),
              ],
            ),
          ),
        ),
        ShapeStroke(color: white, width: AnimatedDouble(3.5)),
      ],
      (q) => Offset(right(q) - buttonInset(q), bottom(q) - 120),
      scale: (q) => _at([
        (96, .7),
        (136, .7),
        (145, 1),
        (174, 1),
        (178, .93),
        (182, 1),
        (190, .8),
        (196, .65),
      ], q),
      effects: [_blur(96, 197, uiBlur)],
    ),
  );
  add(
    _shape(
      'arrow_cursor',
      '03 · Cursor selecionar e enviar',
      113,
      176,
      ShapeLibrary.cursorArrow(),
      (q) => Offset(
        _at([
          (113, 328),
          (117, 354),
          (130, 357),
          (135, 366),
          (138, 220),
          (142, 313),
          (146, 306),
          (151, 311),
          (160, 309),
          (175, 291),
        ], q),
        _at([
          (113, 390),
          (117, 381),
          (132, 392),
          (136, 397),
          (142, 419),
          (144, 354),
          (146, 339),
          (151, 318),
          (160, 319),
          (175, 351),
        ], q),
      ),
      scale: (_) => .68,
      turn: (q) => _at([
        (113, 18),
        (117, 12),
        (136, 4),
        (141, 20),
        (148, 0),
        (175, 0),
      ], q),
      effects: [_blur(113, 176, (q) => q >= 136 && q < 144 ? 4 : 0)],
    ),
  );
  add(
    _shape(
      'click_hand',
      '03 · Cursor clique',
      176,
      197,
      ShapeLibrary.cursorHand(),
      (q) => Offset(right(q) - 97, bottom(q) - 91),
      scale: (q) =>
          _at([(176, .77), (180, .86), (184, .78), (190, .60), (196, .44)], q),
      effects: [_blur(176, 197, uiBlur)],
    ),
  );

  // The card fan enters from above; side cards unfold behind the centre.
  double gy(int q) => _at([
    (196, 150),
    (198, 260),
    (202, 280),
    (206, 314),
    (210, 321),
    (216, 320),
    (224, 317),
    (232, 319),
    (234, 337),
    (236, 351),
    (238, 368),
  ], q);
  double gw(int q) => _at([
    (196, 58),
    (198, 129),
    (202, 149),
    (206, 153),
    (212, 164),
    (220, 171),
    (228, 164),
    (232, 153),
    (234, 138),
    (236, 110),
    (238, 45),
  ], q);
  double galleryBlur(int q) =>
      _at([(196, 15), (200, 4), (203, 0), (234, 0), (238, 12)], q);
  for (final (id, side, start) in [('left', -1.0, 207), ('right', 1.0, 219)]) {
    double unfold(int q) =>
        _at([(start, 0), (start + 3, .8), (start + 8, 1)], q);
    add(
      _image(
        'gallery_$id',
        '04 · Foto $id',
        asset('gallery-$id.png'),
        start,
        239,
        (q) => Offset(
          292 + side * (id == 'left' ? 130 : 102) * unfold(q) * gw(q) / 164,
          gy(q) + 13 * gw(q) / 164,
        ),
        (q) => gw(q) * .94,
        aspect: id == 'left' ? 154 / 224 : 154 / 190,
        turn: (q) =>
            side *
            (id == 'left' ? 1 : 8 / 18) *
            _at([
              (start, 0),
              (start + 3, 21),
              (start + 8, 18),
              (234, 18),
              (238, 0),
            ], q),
        opacity: (q) => unfold(q).clamp(0, 1),
        effects: [
          _blur(
            start,
            239,
            (q) =>
                math.max(galleryBlur(q), _at([(start, 13), (start + 6, 0)], q)),
          ),
        ],
      ),
    );
  }
  add(
    _rect(
      'gallery_border',
      '04 · Moldura central',
      196,
      239,
      (q) => Offset(292, gy(q)),
      (q) => gw(q) + 6,
      (q) => gw(q) * 224 / 154 + 6,
      white,
      radius: 27,
      effects: [_blur(196, 239, galleryBlur)],
    ),
  );
  add(
    _image(
      'gallery_center',
      '04 · Foto central',
      asset('gallery-center.png'),
      196,
      239,
      (q) => Offset(292, gy(q)),
      gw,
      aspect: 154 / 224,
      radius: 23,
      effects: [_blur(196, 239, galleryBlur)],
    ),
  );
  double titleY(int q) => _at([
    (196, -50),
    (200, 35),
    (204, 69),
    (208, 91),
    (216, 92),
    (224, 89),
    (232, 110),
    (236, 145),
    (238, 160),
  ], q);
  double titleScale(int q) => _at([
    (196, .6),
    (202, .94),
    (208, 1),
    (216, 1.02),
    (224, 1.06),
    (232, .95),
    (238, .55),
  ], q);
  add(
    _text(
      'endless',
      '04 · Get endless',
      'Get endless',
      196,
      239,
      (q) => Offset(287, titleY(q)),
      color: ink,
      size: 43,
      scale: titleScale,
      effects: [_blur(196, 239, galleryBlur)],
    ),
  );
  add(
    _text(
      'ideas',
      '04 · Ideas',
      'ideas',
      196,
      239,
      (q) => Offset(287, titleY(q) + 48 * titleScale(q)),
      color: blue,
      size: 45,
      scale: titleScale,
      effects: [_blur(196, 239, galleryBlur)],
    ),
  );
  // Pixel block logo rebuilt as a single editable path collection.
  final logoItems = <ShapeItem>[
    for (final x in [-21.0, 0.0, 21.0])
      ShapeBezier(
        path: AnimatedPath(BezierPath.rect(14, 33, center: Offset(x, -7))),
      ),
    for (final x in [-10.5, 10.5])
      ShapeBezier(
        path: AnimatedPath(BezierPath.rect(14, 20, center: Offset(x, 16))),
      ),
    ShapeFill(color: ink),
  ];
  add(
    _shape(
      'pixel_logo',
      '05 · Simbolo pixel',
      238,
      273,
      logoItems,
      (_) => const Offset(288, 286),
      scale: (q) =>
          _at([(238, .6), (243, 1), (248, 1), (267, 1), (272, .83)], q),
      opacity: (q) => q < 243 ? 0 : 1,
    ),
  );
  add(
    _rect(
      'outro_slice_v',
      '05 · Corte digital vertical',
      235,
      237,
      (_) => const Offset(288, 336),
      (_) => 97,
      (_) => 212,
      dark,
    ),
  );
  add(
    _rect(
      'outro_slice_h',
      '05 · Corte digital horizontal',
      237,
      239,
      (_) => const Offset(357, 288),
      (_) => 232,
      (_) => 75,
      dark,
    ),
  );
  add(
    _rect(
      'outro_square',
      '05 · Pixel preto',
      239,
      241,
      (_) => const Offset(402, 372),
      (_) => 46,
      (_) => 37,
      dark,
    ),
  );
  add(
    _shape(
      'pixel_logo_blue',
      '05 · Simbolo azul luminoso',
      238,
      243,
      [
        ...logoItems.where((i) => i is! ShapeFill),
        ShapeGradientFill(
          colorA: const Color(0xff2389eb),
          colorB: const Color(0xff70fafa),
        ),
      ],
      (_) => const Offset(288, 286),
      effects: [_glow(const Color(0xff4abfff), 45, 50)],
    ),
  );
  add(
    _rect(
      'end_dark',
      '06 · Fundo assinatura',
      273,
      309,
      (_) => const Offset(288, 288),
      (_) => 576,
      (_) => 576,
      const Color(0xff140f14),
    ),
  );
  add(
    _text(
      'signature',
      '06 · Aurea App - RMK Dnyx',
      'Aurea App - RMK Dnyx',
      273,
      309,
      (q) =>
          Offset(288, _at([(273, 272), (278, 281), (291, 283), (308, 287)], q)),
      size: 32,
      scale: (q) => _at([
        (273, 3.3),
        (275, 1.8),
        (278, 1.35),
        (284, 1.1),
        (293, 1),
        (308, .88),
      ], q),
      effects: [
        _glow(white, 65, 100),
        _blur(273, 309, (q) => _at([(273, 7), (276, 2), (280, 0)], q)),
      ],
    ),
  );
  add(
    AudioLayer(
      id: 'dnyx_audio',
      name: '07 · Audio original',
      startTime: Duration.zero,
      duration: dnyxFrame(309),
      sourcePath: asset('audio.m4a'),
    ),
  );
  // The model stores topmost first, while the authoring list above is back-to-front.
  // Dragged media must cover the prompt until it becomes its attachment.
  final portraitLayer = layers.singleWhere((l) => l.id == 'dnyx_portrait');
  final dragHand = layers.singleWhere((l) => l.id == 'dnyx_drag_hand');
  final reactions = layers
      .where((l) => l.id.startsWith('dnyx_reaction_'))
      .toList();
  layers.remove(portraitLayer);
  layers.remove(dragHand);
  layers.removeWhere((l) => reactions.contains(l));
  layers.insertAll(layers.indexWhere((l) => l.id == 'dnyx_send_button'), [
    portraitLayer,
    ...reactions,
    dragHand,
  ]);
  layers.insert(
    layers.indexOf(portraitLayer),
    _rect(
      'photo_halo',
      '02 · Luz azul da foto',
      65,
      97,
      photo,
      (q) => pw(q) + 6,
      (q) => pw(q) * 190 / 128 + 6,
      const Color(0xff2864ff),
      radius: 20,
      opacity: (q) => _at([(65, 0), (70, .36), (88, .34), (96, 0)], q),
      effects: [_blur(65, 97, (_) => 22)],
    ),
  );
  layers.insert(
    3,
    _rect(
      'blue_wash',
      '03 · Pulso azul do arraste',
      103,
      124,
      (_) => const Offset(288, 288),
      (_) => 576,
      (_) => 576,
      const Color(0xffa6d5f5),
      opacity: (q) => _at([(103, 0), (110, .16), (115, .32), (123, 0)], q),
    ),
  );
  final signature =
      layers.singleWhere((l) => l.id == 'dnyx_signature') as TextLayer;
  final bloom = signature.duplicated().copyLayer(
    name: '06 · Halo da assinatura',
    opacity: AnimatedDouble(.65),
    effects: [_blur(273, 309, (_) => 50)],
  );
  layers.insert(layers.indexOf(signature), bloom);
  return VideoProject(
    name: 'Aurea App - RMK Dnyx',
    createdAt: DateTime.now(),
    aspectRatio: 1,
    resolutionHeight: 576,
    fps: 30,
    layers: layers.reversed.toList(),
    meta: {
      for (final l in layers)
        l.id: LayerMeta(folder: l.name.split(' · ').first),
    },
    markers: [
      for (final (q, label) in [
        (0, 'Texto e pixels'),
        (66, 'Foto e reacoes'),
        (96, 'Comando'),
        (140, 'Camera e clique'),
        (196, 'Galeria'),
        (238, 'Simbolo'),
        (273, 'Aurea App - RMK Dnyx'),
      ])
        Marker(time: dnyxFrame(q), label: label),
    ],
  );
}
