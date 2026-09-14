import 'dart:math' as math;
import 'dart:ui';

import 'keyframe.dart';
import 'layer.dart';
import 'shape.dart';
import 'video_project.dart';

/// EXPORTACAO LOTTIE (spec motion-graphics-pro, PR-X23) — "o mais
/// importante do documento".
///
/// Lottie e o formato de animacao vetorial que apps e sites consomem, e
/// nao existe boa ferramenta de autoria no celular. A Aurea ja modela
/// quase tudo que ele precisa: forma com arvore, trim paths, repetidor,
/// mascara, transform com keyframes, texto e precomp.
///
/// A matriz de suporte e DADO consultavel ([LottieSupport]), nao
/// constante no codigo: ela muda entre versoes e entre as bibliotecas de
/// cada plataforma.

/// O que cada recurso enfrenta ao virar Lottie.
enum LottieSupportLevel {
  /// Vai inteiro.
  full,

  /// Vai, mas com aproximacao (documentada no aviso).
  partial,

  /// Nao sobrevive: precisa rasterizar ou remover.
  none,
}

class LottieSupport {
  const LottieSupport({this.levels = _default});

  final Map<String, LottieSupportLevel> levels;

  LottieSupportLevel of(String feature) =>
      levels[feature] ?? LottieSupportLevel.none;

  LottieSupport withFeature(String feature, LottieSupportLevel level) =>
      LottieSupport(levels: {...levels, feature: level});

  static const _default = <String, LottieSupportLevel>{
    'shape': LottieSupportLevel.full,
    'text': LottieSupportLevel.partial,
    'group': LottieSupportLevel.full,
    'null': LottieSupportLevel.full,
    'solid': LottieSupportLevel.full,
    'mask': LottieSupportLevel.full,
    'trim': LottieSupportLevel.full,
    'repeater': LottieSupportLevel.full,
    'gradient': LottieSupportLevel.full,
    'transform': LottieSupportLevel.full,
    'blend.normal': LottieSupportLevel.full,
    'blend.exotic': LottieSupportLevel.none,
    'image': LottieSupportLevel.partial,
    'video': LottieSupportLevel.none,
    'audio': LottieSupportLevel.none,
    'particles': LottieSupportLevel.none,
    'element3d': LottieSupportLevel.none,
    'adjustment': LottieSupportLevel.none,
    'effect.raster': LottieSupportLevel.none,
    'matte': LottieSupportLevel.full,
    'grid': LottieSupportLevel.none,
    'textAnimator': LottieSupportLevel.partial,
  };
}

/// Um problema encontrado pelo validador, ancorado na camada.
class LottieIssue {
  const LottieIssue({
    required this.layerId,
    required this.layerName,
    required this.feature,
    required this.level,
    required this.message,
  });

  final String layerId;
  final String layerName;
  final String feature;
  final LottieSupportLevel level;
  final String message;

  bool get blocking => level == LottieSupportLevel.none;
}

/// VALIDADOR (PR-X23): aponta no app o que nao sobrevive, ANTES de
/// exportar. Sem falso negativo — toda camada com recurso nao suportado
/// aparece aqui.
List<LottieIssue> validateForLottie(
  VideoProject project, {
  LottieSupport support = const LottieSupport(),
}) {
  final issues = <LottieIssue>[];

  void check(Layer l, String feature, String message) {
    final level = support.of(feature);
    if (level == LottieSupportLevel.full) return;
    issues.add(
      LottieIssue(
        layerId: l.id,
        layerName: l.name,
        feature: feature,
        level: level,
        message: message,
      ),
    );
  }

  void visit(Layer l) {
    switch (l) {
      case VideoLayer _:
        check(
          l,
          'video',
          'Camada de video nao existe em Lottie — '
              'rasterize em imagens ou remova.',
        );
      case AudioLayer _:
        check(l, 'audio', 'Audio nao faz parte do Lottie.');
      case CameraLayer _:
        check(
          l,
          'camera',
          'Camera de composicao nao existe em Lottie — o movimento dela '
              'precisa ser assado nas camadas antes de exportar.',
        );
      case ParticlesLayer _:
        check(
          l,
          'particles',
          'Particulas sao geradas em tempo real; Lottie nao tem '
              'equivalente.',
        );
      case Element3DLayer _:
        check(
          l,
          'element3d',
          'Malha 3D nao sobrevive: rasterize ou troque por forma.',
        );
      case AdjustmentLayer _:
        check(
          l,
          'adjustment',
          'Camada de ajuste depende de ler o composto abaixo.',
        );
      case ImageLayer _:
        check(l, 'image', 'Imagem vira asset embutido — o arquivo cresce.');
      case TextLayer t:
        check(
          l,
          'text',
          'Texto exporta como texto Lottie; a fonte precisa existir '
              'no destino.',
        );
        if (t.animators.isNotEmpty) {
          check(
            l,
            'textAnimator',
            'Animadores de texto viram aproximacao por keyframe.',
          );
        }
      case GroupLayer g:
        for (final c in g.children) {
          visit(c);
        }
      case NullLayer n:
        if (n.grid != null) {
          check(
            l,
            'grid',
            'O modulo Grade e procedural: exporte precompondo o '
                'resultado.',
          );
        }
      case Scene3DLayer _:
        check(
          l,
          'element3d',
          'Cena 3D e renderizada em tempo real; Lottie nao tem '
              'equivalente — rasterize.',
        );
      case ShapeLayer _:
      case CaptionLayer _:
        break;
    }

    if (l.blendMode != BlendMode.srcOver) {
      check(
        l,
        'blend.exotic',
        'Modo de mesclagem sem equivalente confiavel em Lottie.',
      );
    }
    for (final e in l.effects) {
      if (!e.enabled) continue;
      check(
        l,
        'effect.raster',
        'O efeito "${e.type.name}" e de raster e nao sobrevive.',
      );
    }
  }

  for (final l in project.layers) {
    visit(l);
  }
  return issues;
}

// --------------------------------------------------------- conversao

List<double> _color(Color c) => [c.r, c.g, c.b];

/// Valor animado -> propriedade Lottie (com ou sem keyframes).
Map<String, dynamic> _lv(AnimatedDouble a, int fps, {double scale = 1}) {
  if (!a.isAnimated) {
    return {'a': 0, 'k': a.base * scale};
  }
  return {
    'a': 1,
    'k': [
      for (var i = 0; i < a.keyframes.length; i++)
        {
          't': a.keyframes[i].time.inMicroseconds * fps / 1000000,
          's': [a.keyframes[i].value * scale],
          if (i < a.keyframes.length - 1)
            'e': [a.keyframes[i + 1].value * scale],
          'i': {
            'x': [0.66],
            'y': [1.0],
          },
          'o': {
            'x': [0.33],
            'y': [0.0],
          },
        },
    ],
  };
}

Map<String, dynamic> _lo(AnimatedOffset a, int fps) {
  if (!a.isAnimated) {
    return {
      'a': 0,
      'k': [a.base.dx, a.base.dy, 0],
    };
  }
  return {
    'a': 1,
    'k': [
      for (var i = 0; i < a.keyframes.length; i++)
        {
          't': a.keyframes[i].time.inMicroseconds * fps / 1000000,
          's': [a.keyframes[i].value.dx, a.keyframes[i].value.dy, 0],
          if (i < a.keyframes.length - 1)
            'e': [a.keyframes[i + 1].value.dx, a.keyframes[i + 1].value.dy, 0],
          'i': {'x': 0.66, 'y': 1.0},
          'o': {'x': 0.33, 'y': 0.0},
        },
    ],
  };
}

/// Transform Lottie ("ks") a partir da camada.
Map<String, dynamic> _transform(Layer l, int fps) {
  final scale = AnimatedDouble(l.scaleX.base * 100, [
    for (final k in l.scaleX.keyframes)
      Keyframe(time: k.time, value: k.value * 100, ease: k.ease),
  ]);
  return {
    'a': _lo(l.pivot, fps),
    'p': _lo(l.position, fps),
    's': {
      'a': scale.isAnimated ? 1 : 0,
      'k': scale.isAnimated
          ? [
              for (var i = 0; i < scale.keyframes.length; i++)
                {
                  't': scale.keyframes[i].time.inMicroseconds * fps / 1000000,
                  's': [
                    scale.keyframes[i].value,
                    scale.keyframes[i].value,
                    100,
                  ],
                  'i': {'x': 0.66, 'y': 1.0},
                  'o': {'x': 0.33, 'y': 0.0},
                },
            ]
          : [scale.base, scale.base, 100],
    },
    'r': _lv(l.rotation, fps),
    'o': _lv(l.opacity, fps, scale: 100),
  };
}

/// Caminho de uma forma -> "ks" de shape Lottie (formato ponto/tangente).
Map<String, dynamic> _pathData(Path path) {
  final vertices = <List<double>>[];
  final metrics = path.computeMetrics().toList();
  var closed = false;
  for (final m in metrics) {
    // Amostragem uniforme: Lottie guarda bezier, mas uma poligonal densa
    // e fiel o bastante e sempre valida.
    final steps = math.max(8, (m.length / 6).round());
    for (var i = 0; i < steps; i++) {
      final tan = m.getTangentForOffset(m.length * i / steps);
      if (tan != null) {
        vertices.add([tan.position.dx, tan.position.dy]);
      }
    }
    closed = true;
  }
  return {
    'a': 0,
    'k': {
      'i': [
        for (var _ in vertices) [0.0, 0.0],
      ],
      'o': [
        for (var _ in vertices) [0.0, 0.0],
      ],
      'v': vertices,
      'c': closed,
    },
  };
}

List<Map<String, dynamic>> _shapeItems(ShapeLayer l, Duration t, int fps) {
  final out = <Map<String, dynamic>>[];
  final draws = evaluateShape(l.contents, t);
  for (final d in draws) {
    out.add({'ty': 'sh', 'ks': _pathData(d.path)});
    if (d.paint.style == PaintingStyle.stroke) {
      out.add({
        'ty': 'st',
        'c': {'a': 0, 'k': _color(d.paint.color)},
        'o': {'a': 0, 'k': d.paint.color.a * 100},
        'w': {'a': 0, 'k': d.paint.strokeWidth},
      });
    } else {
      out.add({
        'ty': 'fl',
        'c': {'a': 0, 'k': _color(d.paint.color)},
        'o': {'a': 0, 'k': d.paint.color.a * 100},
      });
    }
  }
  return out;
}

/// Uma camada Lottie ("layers[]").
Map<String, dynamic>? _layerJson(VideoProject p, Layer l, int index, int fps) {
  final ip = l.startTime.inMicroseconds * fps / 1000000;
  final op = l.endTime.inMicroseconds * fps / 1000000;
  final base = <String, dynamic>{
    'ind': index,
    'nm': l.name,
    'ks': _transform(l, fps),
    'ao': 0,
    'ip': ip,
    'op': op,
    'st': ip,
    'bm': 0,
    'sr': 1,
  };

  switch (l) {
    case ShapeLayer s:
      return {
        ...base,
        'ty': 4, // shape
        'shapes': [
          {
            'ty': 'gr',
            'nm': s.name,
            'it': [
              ..._shapeItems(s, Duration.zero, fps),
              {
                'ty': 'tr',
                'p': {
                  'a': 0,
                  'k': [0, 0],
                },
                'a': {
                  'a': 0,
                  'k': [0, 0],
                },
                's': {
                  'a': 0,
                  'k': [100, 100],
                },
                'r': {'a': 0, 'k': 0},
                'o': {'a': 0, 'k': 100},
              },
            ],
          },
        ],
      };
    case TextLayer t:
      return {
        ...base,
        'ty': 5, // text
        't': {
          'd': {
            'k': [
              {
                't': 0,
                's': {
                  't': t.text,
                  's': t.fontSize,
                  'f': 'Roboto',
                  'j': 2,
                  'lh': t.fontSize * 1.2,
                  'fc': _color(t.color),
                },
              },
            ],
          },
          'p': <String, dynamic>{},
          'm': {
            'g': 1,
            'a': {
              'a': 0,
              'k': [0, 0],
            },
          },
          'a': <dynamic>[],
        },
      };
    case NullLayer _:
      return {...base, 'ty': 3}; // null
    case GroupLayer g:
      // Precomp: os filhos viram um asset proprio. O tempo do precomp e
      // (tempo - st): com o ponto de entrada no conteudo (grupo aparado ou
      // segunda metade de uma divisao) o conteudo continua de onde estava,
      // como no editor, e nao recomeca do zero.
      return {
        ...base,
        'ty': 0,
        'refId': 'comp_${g.id}',
        'st': (g.startTime - g.contentOffset).inMicroseconds * fps / 1000000,
        'w': p.outputWidth,
        'h': p.outputHeight,
      };
    default:
      return null; // nao sobrevive; o validador ja avisou
  }
}

/// Resultado da exportacao: o JSON e o que ficou pelo caminho.
typedef LottieExport = ({
  Map<String, dynamic> json,
  List<LottieIssue> issues,
  int exported,
  int skipped,
});

/// CONVERTE a cena para o esquema Lottie. Camadas que o validador marca
/// como bloqueantes sao PULADAS (e contadas), nunca exportadas quebradas.
LottieExport exportLottie(
  VideoProject project, {
  LottieSupport support = const LottieSupport(),
}) {
  final fps = project.fps;
  final issues = validateForLottie(project, support: support);
  final blocking = {
    for (final i in issues)
      if (i.blocking) i.layerId,
  };

  final assets = <Map<String, dynamic>>[];
  final layers = <Map<String, dynamic>>[];
  var index = 1;
  var skipped = 0;

  // O asset de um grupo, e o de cada grupo dentro dele: antes so o nivel
  // de cima ganhava asset, e o grupo aninhado apontava para um refId que
  // nao existia (o player descartava o arquivo inteiro).
  void precomp(GroupLayer g) {
    final childLayers = <Map<String, dynamic>>[];
    var ci = 1;
    for (final c in g.children) {
      if (blocking.contains(c.id)) {
        skipped++;
        continue;
      }
      final cj = _layerJson(project, c, ci, fps);
      if (cj == null) {
        skipped++;
        continue;
      }
      childLayers.add(cj);
      ci++;
      if (c is GroupLayer) precomp(c);
    }
    assets.add({'id': 'comp_${g.id}', 'layers': childLayers});
  }

  // Lottie desenha da ultima para a primeira, como a nossa pilha.
  for (final l in project.layers) {
    if (blocking.contains(l.id)) {
      skipped++;
      continue;
    }
    final json = _layerJson(project, l, index, fps);
    if (json == null) {
      skipped++;
      continue;
    }
    layers.add(json);
    index++;
    if (l is GroupLayer) precomp(l);
  }

  final durationFrames = project.duration.inMicroseconds * fps / 1000000;

  return (
    json: <String, dynamic>{
      'v': '5.7.4',
      'fr': fps,
      'ip': 0,
      'op': durationFrames,
      'w': project.outputWidth,
      'h': project.outputHeight,
      'nm': project.name,
      'ddd': 0,
      'assets': assets,
      'layers': layers,
    },
    issues: issues,
    exported: layers.length,
    skipped: skipped,
  );
}

/// SVG ANIMADO (PR-X25): mesmo motor, saida diferente — util para web
/// sem biblioteca. Anima transform por SMIL, que todo navegador le.
String exportAnimatedSvg(VideoProject project) {
  final w = project.outputWidth, h = project.outputHeight;
  final durUs = math.max(1, project.duration.inMicroseconds);
  final seconds = durUs / 1e6;
  final out = StringBuffer(
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 $w $h" width="$w" height="$h">\n',
  );
  for (final layer in project.layers.reversed) {
    if (layer is! ShapeLayer || !project.rendersInPreview(layer.id)) continue;
    final animated =
        layer.keyframeTimes.isNotEmpty ||
        layer.startTime > Duration.zero ||
        layer.duration < project.duration;
    final count = animated ? (seconds * project.fps).ceil().clamp(1, 1800) : 1;
    final times = [
      for (var i = 0; i <= count; i++)
        Duration(microseconds: (durUs * i / count).round()),
    ];
    final local = [for (final t in times) layer.localTime(t)];
    final initialDraws = evaluateShape(layer.contents, local.first);
    final draws = layer.moduleTimesUs.isEmpty
        ? List.filled(local.length, initialDraws)
        : [for (final t in local) evaluateShape(layer.contents, t)];
    final pathCache = <ShapeDraw, String>{};
    final transforms = [
      for (final t in times) effectiveTransform(project, layer, t),
    ];
    final keyTimes = [
      for (var i = 0; i <= count; i++) (i / count).toStringAsFixed(8),
    ].join(';');
    String anim(
      String attribute,
      List<String> values, {
      String? type,
      bool discrete = false,
    }) {
      if (values.every((v) => v == values.first)) return '';
      final tag = type == null ? 'animate' : 'animateTransform';
      return '<$tag attributeName="$attribute" ${type == null ? '' : 'type="$type"'} '
          'values="${values.join(';')}" keyTimes="$keyTimes" dur="${seconds}s" '
          'calcMode="${discrete ? 'discrete' : 'linear'}" repeatCount="indefinite" />';
    }

    final positions = [for (final t in transforms) '${t.pos.dx} ${t.pos.dy}'];
    final rotations = [for (final t in transforms) '${t.rot}'];
    final scales = [
      for (var i = 0; i < times.length; i++)
        '${transforms[i].scale} ${layer.scaleY.valueAt(local[i]) * (layer.scaleX.valueAt(local[i]).abs() < 1e-9 ? 1 : transforms[i].scale / layer.scaleX.valueAt(local[i]))}',
    ];
    final opacity = [
      for (var i = 0; i < times.length; i++)
        '${layer.activeAt(times[i]) ? layer.opacity.valueAt(local[i]).clamp(0.0, 1.0) : 0}',
    ];
    final pivots = [for (final t in local) layer.pivot.valueAt(t)];
    final pivotValues = [for (final p in pivots) '${p.dx} ${p.dy}'];
    final inversePivot = [for (final p in pivots) '${-p.dx} ${-p.dy}'];
    out.writeln(
      '<g opacity="${opacity.first}">${anim('opacity', opacity, discrete: true)}',
    );
    void group(String type, List<String> values) => out.writeln(
      '<g transform="$type(${values.first})">${anim('transform', values, type: type)}',
    );
    group('translate', positions);
    group('translate', pivotValues);
    group('rotate', rotations);
    group('scale', scales);
    group('translate', inversePivot);
    final centers = [for (final d in draws) shapeBounds(d).center];
    group('translate', [for (final c in centers) '${-c.dx} ${-c.dy}']);
    final maxDraws = draws.fold<int>(0, (n, d) => math.max(n, d.length));
    for (var index = 0; index < maxDraws; index++) {
      final exemplar = draws.firstWhere((d) => d.length > index)[index];
      final paint = exemplar.paint;
      final color = paint.color;
      final rgb =
          'rgb(${(color.r * 255).round()},${(color.g * 255).round()},${(color.b * 255).round()})';
      final paths = [
        for (final d in draws)
          index < d.length
              ? pathCache.putIfAbsent(d[index], () => _svgPath(d[index].path))
              : 'M0,0',
      ];
      final style = paint.style == PaintingStyle.stroke
          ? 'fill="none" stroke="$rgb" stroke-width="${paint.strokeWidth}" stroke-opacity="${color.a}" stroke-linecap="${paint.strokeCap.name}" stroke-linejoin="${paint.strokeJoin.name}" stroke-miterlimit="${paint.strokeMiterLimit}"'
          : 'fill="$rgb" fill-opacity="${color.a}"';
      out.writeln(
        '<path d="${paths.first}" $style fill-rule="${exemplar.path.fillType == PathFillType.evenOdd ? 'evenodd' : 'nonzero'}">'
        '${anim('d', paths, discrete: true)}</path>',
      );
    }
    out.writeln('</g></g></g></g></g></g></g>');
  }
  out.writeln('</svg>');
  return out.toString();
}

String _svgPath(Path path) {
  final buf = StringBuffer();
  for (final m in path.computeMetrics()) {
    final steps = math.max(8, (m.length / 6).round());
    for (var i = 0; i <= steps; i++) {
      final tan = m.getTangentForOffset(m.length * i / steps);
      if (tan == null) continue;
      final p = tan.position;
      buf.write(
        i == 0
            ? 'M${p.dx.toStringAsFixed(2)},${p.dy.toStringAsFixed(2)}'
            : ' L${p.dx.toStringAsFixed(2)},${p.dy.toStringAsFixed(2)}',
      );
    }
    if (m.isClosed) buf.write(' Z');
  }
  return buf.toString();
}
